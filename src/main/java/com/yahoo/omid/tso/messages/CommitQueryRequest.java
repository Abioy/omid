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

package com.yahoo.omid.tso.messages;

import java.io.DataOutputStream;
import java.io.IOException;

import org.jboss.netty.buffer.ChannelBuffer;

import com.yahoo.omid.tso.TSOMessage;


public class CommitQueryRequest extends SizedTSOMessage implements TSOMessage {
   public long startTimestamp;
   public long queryTimestamp;
//   public RowKey rowkey;
   
   public CommitQueryRequest() {
//      rowkey = new RowKey();;
   }

   public CommitQueryRequest(long startTimestamp, long queryTimestamp) {
      this.startTimestamp = startTimestamp;
      this.queryTimestamp = queryTimestamp;
//      this.rowkey = rowkey;
   }

   @Override
   public void readObject(ChannelBuffer aInputStream)
      throws IOException {
      startTimestamp = aInputStream.readLong();
      queryTimestamp = aInputStream.readLong();
//      rowkey = RowKey.readObject(aInputStream);
   }

   @Override
   public void writeObject(DataOutputStream aOutputStream)
      throws IOException {
      aOutputStream.writeLong(startTimestamp);
      aOutputStream.writeLong(queryTimestamp);
//      rowkey.writeObject(aOutputStream);
   }

   @Override
   public String toString() {
      return "CommitQueryRequest["+startTimestamp+","+queryTimestamp+"]";
   }

   @Override
   public void writeObject(ChannelBuffer buffer)  {
   }
}
