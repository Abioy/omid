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

package com.yahoo.omid.client;

public class SyncCommitQueryCallback extends SyncCallbackBase
   implements CommitQueryCallback {
   private boolean committed = false;
   private long commitTimestamp;
   private boolean retry = false;

   public boolean isAClearAnswer() {
      return (retry != true);
   }

   //valid only if isAClearAnswer returns true
   public boolean isCommitted() {
      return committed;
   }

   //valid only if isCommitted return true
   public long commitTimestamp() {
      return commitTimestamp;
   }

   synchronized
   public void complete(boolean committed, long commitTimestamp, boolean retry) {
      this.committed = committed;
      this.commitTimestamp = commitTimestamp;
      this.retry = retry;
      countDown();
   }
}
