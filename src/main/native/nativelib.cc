/**
 * Copyright (c) 2011 Yahoo! Inc. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. See accompanying LICENSE file.
 */

#include <jni.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#define _MULTI_THREADED
#include <pthread.h>

#include "com_yahoo_omid_tso_CommitHashMap.h"

#define MAX_KEY_SIZE 256
#define FREE 0
#define OWNED 1

/**
 * The load factor for the hashtable.
 */
long largestOrder = 1;
/*
 * keep statistics of the # of average memory access
 */
long totalwalkforget = 0;
long totalwalkforput = 0;
long totalget = 0;
long totalput = 0;
long gmaxCommits = 0;
/**
 * The total number of entries in the hash table.
 */
int count = 0;
struct Entry;
struct LargeEntry;
struct StartCommit;
//the hash map
LargeEntry (*table);
StartCommit (*commitTable);

//for evicted item from the lastcommit hashmap, I just need to maintain it locally
   /**
    * Largest Deleted Timestamp from the hashmap.lastcommit list
    * This timestamp does not have to be reported to the client
    * this is because the lastcommit list is not replicated to the client
    * this is used for internal consistency only
    */
jlong tmaxForConflictChecking = 0;
pthread_mutex_t  tmaxMutex;

int tableLength;
/**
 * An entry could be garbage collected if its older than this threshold  
 * (The value of this field is (int)(capacity * loadFactor).)
 */

int threshold;

/*
 * Class:     com_yahoo_omid_CommitHashMap
 * Method:    gettotalput
 * Signature: ()J
 */
JNIEXPORT jlong JNICALL Java_com_yahoo_omid_tso_CommitHashMap_gettotalput
(JNIEnv * env, jclass jcls) {
   return totalput;
}

/*
 * Class:     com_yahoo_omid_CommitHashMap
 * Method:    gettotalget
 * Signature: ()J
 */
JNIEXPORT jlong JNICALL Java_com_yahoo_omid_tso_CommitHashMap_gettotalget
(JNIEnv * env, jclass jcls) {
   return totalget;
}

/*
 * Class:     com_yahoo_omid_CommitHashMap
 * Method:    gettotalwalkforput
 * Signature: ()J
 */
JNIEXPORT jlong JNICALL Java_com_yahoo_omid_tso_CommitHashMap_gettotalwalkforput
(JNIEnv * env, jclass jcls) {
   return totalwalkforput;
}

/*
 * Class:     com_yahoo_omid_CommitHashMap
 * Method:    gettotalwalkforget
 * Signature: ()J
 */
JNIEXPORT jlong JNICALL Java_com_yahoo_omid_tso_CommitHashMap_gettotalwalkforget
(JNIEnv * env, jclass jcls) {
   return totalwalkforget;
}

/*
 * Each item that stores the start timestamp and commit timestamp
 */
struct StartCommit {
   pthread_mutex_t  mutex;
   jlong start;//which is the start timestamp;
   jlong commit;//which is commit timestamp
};

/**
 * Innerclass that acts as a datastructure to create a new entry in the
 * table.
 * Avoid using object to use less memory walks
 */
struct Entry {
   long order;//the assigned order after insert
   int hash;//keep the computed hash for efficient comparison of keys
   //jbyte key[8];//which is row id;
   jbyte* key;//which is row id concatinated into table id
   jbyte rowidsize;//the key size = rowidsize + tableidsize
   jbyte tableidsize;//
   //jlong tag;//which is the start timestamp;
   jlong value;//which is commit timestamp
   //important must be the last field because of memcpy
   Entry* next;

   Entry() {
      order = 0;
      //hash = 0;
      key = NULL;
      //tag = 0;
      value = 0;
      next = NULL;
   }

   Entry(Entry* e) {
      *this = *e;
   }
};

struct LargeEntry {
   pthread_mutex_t  mutex;
   unsigned char owned;
   Entry e1;
   Entry e2;
   Entry e3;

   LargeEntry() {
      e1.next = &e2;
      e2.next = &e3;
   }
};

/*
 * Class:     CommitHashMap
 * Method:    init
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_com_yahoo_omid_tso_CommitHashMap_init
(JNIEnv * env, jobject jobj, jint initialCapacity, jint maxCommits,jfloat loadFactor) {
   tableLength = initialCapacity;
   threshold = (int) (initialCapacity * loadFactor);
   printf("initialCapacity %d\n", initialCapacity);
   printf("Entry %lu Entry* %lu long %lu int %lu char %lu jbyte %lu jbyte* %lu jlong %lu\n", 
         sizeof(Entry), sizeof(Entry*), sizeof(long), sizeof(int), sizeof(char), sizeof(jbyte), sizeof(jbyte*), sizeof(jlong));
   table = new LargeEntry[initialCapacity];
   commitTable = new StartCommit[maxCommits];
   memset(commitTable, 0, sizeof(StartCommit)*maxCommits);
   gmaxCommits = maxCommits;

   //Initialze the table not to interfere with garbage collection
   printf("Entry* %p %%8 %lu ...\n", table, ((size_t)table)%8);
   printf("MEMORY initialization start ...\n");
   for (int i = 0; i < initialCapacity; i++) {
       table[i].owned = FREE;
      pthread_mutex_init(&table[i].mutex, NULL);
      if (i%1000000==0) {
         printf("MEMORY i=%d\n", i);
         fflush(stdout);
      }
      //Entry* e2 = new Entry();
      //Entry* e3 = new Entry();
      //table[i].next = e2;
      //e2->next = e3;
   }
   for (int i = 0; i < maxCommits; i++) {
      pthread_mutex_init(&commitTable[i].mutex, NULL);
   }
   pthread_mutex_init(&tmaxMutex, NULL);

   printf("MEMORY initialization end\n");
   fflush(stdout);
}


// Returns the value to which the specified key is mapped in this map.
// If there are multiple values with the same key, return the first
// The first is the one with the largest key, because (i) put always
// put the recent ones ahead, (ii) a new put on the same key has always
// larger value (because value is commit timestamp and the map is atmoic)
//
// @param   key   a key in the hashtable.
// @return  the value to which the key is mapped in this hashtable;
//          <code>NULL</code> if the key is not mapped to any value in
//          this hashtable.

/*
 * Class:     CommitHashMap
 * Method:    get
 * Signature: (JI)J
 */

/*
 * The difference with lock is that (i) it checks for tmax internally 
 * (ii) it fails if it already has a distributed owner. 
 * The latter is important specially in the case of distributed locks
 */
JNIEXPORT jboolean JNICALL Java_com_yahoo_omid_tso_CommitHashMap_lock
(JNIEnv * env , jobject jobj, jint index, jlong startTimestamp) {
   int rc = pthread_mutex_lock(&table[index].mutex);
   jlong tmax = tmaxForConflictChecking;
   if (tmax > startTimestamp)
       return JNI_FALSE;
   if (table[index].owned != FREE)
       return JNI_FALSE;
   return JNI_TRUE;
}


JNIEXPORT jlong JNICALL Java_com_yahoo_omid_tso_CommitHashMap_unlock__I
(JNIEnv * env , jobject jobj, jint index) {
   int rc = pthread_mutex_unlock(&table[index].mutex);
   return 0;
}

JNIEXPORT jlong JNICALL Java_com_yahoo_omid_tso_CommitHashMap_unlock__IZ
(JNIEnv * env , jobject jobj, jint index, jboolean keepItOwned) {
   table[index].owned = keepItOwned == JNI_TRUE ? !FREE : FREE;
   int rc = pthread_mutex_unlock(&table[index].mutex);
   return 0;
}

JNIEXPORT jlong JNICALL Java_com_yahoo_omid_tso_CommitHashMap_atomicget
(JNIEnv * env , jobject jobj, jbyteArray rowId, jbyteArray tableId, jint hash, jint index, jlong ts) {
   jboolean lockres = Java_com_yahoo_omid_tso_CommitHashMap_lock(env, jobj, index, ts);
   jlong res = Java_com_yahoo_omid_tso_CommitHashMap_get(env, jobj, rowId, tableId, hash);
   //if tmaxForConflictChecking is larger than the start timestamp, then return -1 to indicate abort
   //if (tmax > ts)//assme(ts >= 0)
   if (lockres == JNI_FALSE)
      res = -1;
   Java_com_yahoo_omid_tso_CommitHashMap_unlock__I(env, jobj, index);
   return res;
}

jbyte keyarray[MAX_KEY_SIZE];
JNIEXPORT jlong JNICALL Java_com_yahoo_omid_tso_CommitHashMap_get
(JNIEnv * env , jobject jobj, jbyteArray rowId, jbyteArray tableId, jint hash) {
   totalget++;
   jsize rowidsize  = env->GetArrayLength(rowId);
   jsize tableidsize  = env->GetArrayLength(tableId);
   env->GetByteArrayRegion(rowId,0,rowidsize,keyarray);
   env->GetByteArrayRegion(tableId,0,tableidsize,keyarray + rowidsize * sizeof(jbyte));
   char keyarraysize = (rowidsize + tableidsize) * sizeof(jbyte);
   int index = (hash & 0x7FFFFFFF) % tableLength;

   //int   rc;
   //rc = pthread_mutex_lock(&table[index].mutex);
   for (Entry* e = &(table[index].e1); e != NULL; e = e->next) {
      totalwalkforget++;
      if (e->order == 0)//empty
         break;
      if (e->hash == hash && e->rowidsize == rowidsize && e->tableidsize == tableidsize)
         if (memcmp(e->key, keyarray, keyarraysize)==0) {
            //rc = pthread_mutex_unlock(&table[index].mutex);
            return e->value;
         }
   }
   //rc = pthread_mutex_unlock(&table[index].mutex);
   return 0;
}

/*
 * Class:     CommitHashMap
 * Method:    put
 * Signature: (JJJI)Z
 */
JNIEXPORT void JNICALL Java_com_yahoo_omid_tso_CommitHashMap_put
(JNIEnv * env , jobject jobj, jbyteArray rowId, jbyteArray tableId, jlong value, jint hash) {
   totalput++;
   int index = (hash & 0x7FFFFFFF) % tableLength;
   Entry* firstBucket = &(table[index].e1);
   bool keyarrayloaded = false;
   jsize rowidsize, tableidsize;
   unsigned int keyarraysize;

   int   rc;
   //rc = pthread_mutex_lock(&table[index].mutex);

   Entry* lastEntry = NULL;//after the loop, it points to the last entry
   for (Entry* e = firstBucket; e != NULL; lastEntry = e, e = e->next) {
      totalwalkforput++;

      //int pl = largestOrder;
      //long po = e->order;
      //int pt = threshold;
      bool isOld = e->order == 0 ? 
         true :
         largestOrder - e->order > threshold;
      //usleep(1);

      if (isOld) {

         int rc = pthread_mutex_lock(&tmaxMutex);
         if (e->value > tmaxForConflictChecking) {
            tmaxForConflictChecking = e->value;
         }
         rc = pthread_mutex_unlock(&tmaxMutex);

         if (keyarrayloaded == false) {
            rowidsize  = env->GetArrayLength(rowId);
            tableidsize  = env->GetArrayLength(tableId);
         }
         if (e->key == NULL || (e->rowidsize + e->tableidsize) < (rowidsize + tableidsize)) {//not reusable 
            free(e->key);
            //jbyte* key = (jbyte *)malloc(len * sizeof(jbyte));
            e->key = (jbyte *)malloc((rowidsize + tableidsize) * sizeof(jbyte));
         }
         if (keyarrayloaded == false) {
            env->GetByteArrayRegion(rowId,0,rowidsize,e->key);
            env->GetByteArrayRegion(tableId,0,tableidsize,e->key + rowidsize * sizeof(jbyte));
         }
         else memcpy(e->key, keyarray, keyarraysize);

         e->rowidsize = rowidsize;
         e->tableidsize = tableidsize;
         e->hash = hash;
         //e->tag = tag;
         e->value = value;
         e->order = ++largestOrder;
   //rc = pthread_mutex_unlock(&table[index].mutex);
         return;// largestDeletedTimestamp;
      }

      if (keyarrayloaded == false) {
         rowidsize  = env->GetArrayLength(rowId);
         tableidsize  = env->GetArrayLength(tableId);
         keyarraysize = (rowidsize + tableidsize) * sizeof(jbyte);
         env->GetByteArrayRegion(rowId,0,rowidsize,keyarray);
         env->GetByteArrayRegion(tableId,0,tableidsize,keyarray + rowidsize * sizeof(jbyte));
         keyarrayloaded = true;
      }
      if (e->hash == hash && e->rowidsize == rowidsize && e->tableidsize == tableidsize) {
         if (memcmp(e->key, keyarray, keyarraysize)==0)  {
            //e->tag = tag;
            e->value = value;
            e->order = ++largestOrder;
   //rc = pthread_mutex_unlock(&table[index].mutex);
            return; //largestDeletedTimestamp;
         }
      }
   }

   //printf("new entry");
   // Creates the new entry.
   LargeEntry* le = new LargeEntry();
   Entry* newentry = &(le->e1);
   lastEntry->next = newentry;
   if (keyarrayloaded == false) {
      rowidsize  = env->GetArrayLength(rowId);
      tableidsize  = env->GetArrayLength(tableId);
   }
   newentry->key = (jbyte *)malloc((rowidsize + tableidsize) * sizeof(jbyte));
   if (keyarrayloaded == false) {
      env->GetByteArrayRegion(rowId,0,rowidsize,newentry->key);
      env->GetByteArrayRegion(tableId,0,tableidsize,newentry->key + rowidsize * sizeof(jbyte));
   }
   else memcpy(newentry->key, keyarray, keyarraysize);
   newentry->rowidsize = rowidsize;
   newentry->tableidsize = tableidsize;
   newentry->hash = hash;
   //newentry->tag = tag;
   newentry->value = value;
   newentry->order = ++largestOrder;
   //newentry->next = e;
   if (count % 100000 == 0) {
      printf("NNNNNNNNNNNNNNNNNNNew Entry %d\n" , count);
      fflush(stdout);
   }
   count++;
   //rc = pthread_mutex_unlock(&table[index].mutex);
   return; // largestDeletedTimestamp;
}


JNIEXPORT jlong JNICALL Java_com_yahoo_omid_tso_CommitHashMap_getCommittedTimestamp(JNIEnv *, jobject, jlong startTimestamp) {
   int key = startTimestamp % gmaxCommits;
   StartCommit& entry = commitTable[key];
   int   rc;
   rc = pthread_mutex_lock(&entry.mutex);
   if (entry.start == startTimestamp) {
      rc = pthread_mutex_unlock(&entry.mutex);
      return entry.commit;
   }
   rc = pthread_mutex_unlock(&entry.mutex);
   return 0;//which means that there is not such entry in the array, either deleted or never entered
}

JNIEXPORT jlong JNICALL Java_com_yahoo_omid_tso_CommitHashMap_setCommitted(JNIEnv * env , jobject jobj, jlong startTimestamp, jlong commitTimestamp, jlong largestDeletedTimestamp) {
   int key = startTimestamp % gmaxCommits;
   StartCommit& entry = commitTable[key];
   int   rc;
   rc = pthread_mutex_lock(&entry.mutex);
   //assume(entry.start != startTimestamp);
   if (entry.start != startTimestamp && entry.commit > largestDeletedTimestamp)
      largestDeletedTimestamp = entry.commit;
   entry.start = startTimestamp;
   entry.commit = commitTimestamp;
   rc = pthread_mutex_unlock(&entry.mutex);
   return largestDeletedTimestamp;
}


