#include <regex>
#include <mutex>
#include <vector>
#include <string>
#include <thread>
#include <cstdio>
#include <chrono>
#include <errno.h>
#include <csignal>
#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <getopt.h>
#include <unistd.h>
#include <limits.h>
#include <sys/inotify.h>
#include <grpcpp/grpcpp.h>
#include <utime.h>

#include "src/dfs-utils.h"
#include "src/dfslibx-clientnode-p2.h"
#include "dfslib-shared-p2.h"
#include "dfslib-clientnode-p2.h"
#include "proto-src/dfs-service.grpc.pb.h"

using grpc::Status;
using grpc::Channel;
using grpc::StatusCode;
using grpc::ClientWriter;
using grpc::ClientReader;
using grpc::ClientContext;

extern dfs_log_level_e DFS_LOG_LEVEL;

//
// STUDENT INSTRUCTION:
//
// Change these "using" aliases to the specific
// message types you are using to indicate
// a file request and a listing of files from the server.
//
using FileRequestType = dfs_service::FileRequest;
using FileListResponseType = dfs_service::FileList;

std::mutex sync_mutex;

DFSClientNodeP2::DFSClientNodeP2() : DFSClientNode() {}
DFSClientNodeP2::~DFSClientNodeP2() {}

grpc::StatusCode DFSClientNodeP2::RequestWriteAccess(const std::string &filename) {

    ClientContext context;
    dfs_service::WriteRequest request;
    dfs_service::ResponseStatus stat;

    std::chrono::system_clock::time_point deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout);
    context.set_deadline(deadline);

    request.set_filename(filename);
    request.set_clientid(client_id);

    Status status = service_stub->WriteAccess(&context, request, &stat);

    if (!status.ok()) {
        // If the RPC failed, return the appropriate StatusCode
        if (status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED) {
            std::cout << "Request timed out. Server may be slow to start or unavailable." << std::endl;
            return StatusCode::DEADLINE_EXCEEDED;
        }else if(status.error_code() == grpc::StatusCode::RESOURCE_EXHAUSTED){
            std::cout << "RESOURCE_EXHAUSTED" << std::endl;
            return StatusCode::RESOURCE_EXHAUSTED;
        }
        else{
            std::cout << "RPC Failed with status: " << status.error_message() << std::endl;
            return StatusCode::CANCELLED;
        }
    }

    std::cout << "Write lock granted for file: " << filename << std::endl;

    return StatusCode::OK;

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to obtain a write lock here when trying to store a file.
    // This method should request a write lock for the given file at the server,
    // so that the current client becomes the sole creator/writer. If the server
    // responds with a RESOURCE_EXHAUSTED response, the client should cancel
    // the current file storage
    //
    // The StatusCode response should be:
    //
    // OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::RESOURCE_EXHAUSTED - if a write lock cannot be obtained
    // StatusCode::CANCELLED otherwise
    //
    //

}


grpc::StatusCode DFSClientNodeP2::UnlockWriteAccess(const std::string &filename) {

    std::cout << "unlock is called" << std::endl;

    ClientContext context;
    dfs_service::WriteRequest request;
    dfs_service::ResponseStatus stat;

    std::chrono::system_clock::time_point deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout);
    context.set_deadline(deadline);

    request.set_filename(filename);
    request.set_clientid(client_id);

    Status status = service_stub->UnlockWriteAccess(&context, request, &stat);

    if (!status.ok()) {
        // If the RPC failed, return the appropriate StatusCode
        if (status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED) {
            std::cout << "Request timed out. Server may be slow to start or unavailable." << std::endl;
            return StatusCode::DEADLINE_EXCEEDED;
        }else if(status.error_code() == grpc::StatusCode::RESOURCE_EXHAUSTED){
            std::cout << "RESOURCE_EXHAUSTED" << std::endl;
            return StatusCode::RESOURCE_EXHAUSTED;
        }
        else{
            std::cout << "RPC Failed with status: " << status.error_message() << std::endl;
            return StatusCode::CANCELLED;
        }
    }

    std::cout << "Unlock Write lock granted for file: " << filename << std::endl;

    return StatusCode::OK;

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to obtain a write lock here when trying to store a file.
    // This method should request a write lock for the given file at the server,
    // so that the current client becomes the sole creator/writer. If the server
    // responds with a RESOURCE_EXHAUSTED response, the client should cancel
    // the current file storage
    //
    // The StatusCode response should be:
    //
    // OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::RESOURCE_EXHAUSTED - if a write lock cannot be obtained
    // StatusCode::CANCELLED otherwise
    //
    //

}

grpc::StatusCode DFSClientNodeP2::Store(const std::string &filename) {
    //connect to the proto
    ClientContext context;
    dfs_service::ResponseStatus responsestat;

    //set deadline
    std::chrono::system_clock::time_point deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout);
    context.set_deadline(deadline);

    //open the file
    //https://en.cppreference.com/w/cpp/io/basic_ifstream
    //read file in binary mode;
    std::string WrappedPath = WrapPath(filename);
    std::ifstream file(WrappedPath, std::ios::binary);
    std::size_t bytes_sent = 0;

    if (!file) {
        std::cerr << "FILE_NOT_FOUND: " << filename << std::endl;
        return StatusCode::CANCELLED;  // or another appropriate error code
    }

    /*write to server if  write acess is gained*/

    //https://stackoverflow.com/questions/20911584/how-to-read-a-file-in-multiple-chunks-until-eof-c
    // read file till the EOF
    std::unique_ptr<ClientWriter<dfs_service::StoreRequest>> writer(service_stub->Store(&context, &responsestat));

    /* try to gain write access */
    StatusCode write_lock = RequestWriteAccess(filename);
    if(write_lock != grpc::StatusCode::OK) {
        return write_lock;
    }

    std::uint32_t crc = dfs_file_checksum(WrappedPath, &crc_table);

    const size_t chunksize = 2024;
    std::vector<char> buffer (chunksize);

    
    //Stream until the end of the file.
    while(!file.eof()){

        dfs_service::StoreRequest request;

        file.read(buffer.data(), buffer.size());

        request.set_filename(filename);

        request.set_data(buffer.data(), file.gcount());

        request.set_crc(crc);

        bytes_sent += file.gcount();

        if (!writer->Write(request)){
            StatusCode write_unlock = UnlockWriteAccess(filename);
            if(write_unlock != grpc::StatusCode::OK) {
                return write_lock;
            }
            std::cout << "SERVER UNAVAILABLE" << std::endl;
            return StatusCode::CANCELLED;
        }
    } 

    file.close();

    if(!writer->WritesDone()){
        StatusCode write_unlock = UnlockWriteAccess(filename);
        if(write_unlock != grpc::StatusCode::OK) {
            return write_lock;
        }
         std::cout << "WriteDone Error" << std::endl;
        return StatusCode::CANCELLED;
    }

    Status status = writer->Finish();  // Status returned here indicates whether the RPC was successful

    StatusCode write_unlock = UnlockWriteAccess(filename);
    if(write_unlock != grpc::StatusCode::OK) {
        return write_lock;
    }

    if (!status.ok()) {
        // If the RPC failed, return the appropriate StatusCode
        if (status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED) {
            std::cout << "Request timed out. Server may be slow to start or unavailable." << std::endl;
            return StatusCode::DEADLINE_EXCEEDED;
        }
        else if(status.error_code() == grpc::StatusCode::ALREADY_EXISTS){
            std::cout << "No Change To The File" << std::endl;
            return StatusCode::ALREADY_EXISTS;
        }
        else{
            std::cout << "RPC Failed with status: " << status.error_message() << std::endl;
            return StatusCode::CANCELLED;
        }
    }

    std::cout << "rpc complete"<< std::endl;
    return StatusCode::OK;


    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to store a file here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // You can start with your Part 1 implementation. However, you will
    // need to adjust this method to recognize when a file trying to be
    // stored is the same on the server (i.e. the ALREADY_EXISTS gRPC response).
    //
    // You will also need to add a request for a write lock before attempting to store.
    //
    // If the write lock request fails, you should return a status of RESOURCE_EXHAUSTED
    // and cancel the current operation.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::ALREADY_EXISTS - if the local cached file has not changed from the server version
    // StatusCode::RESOURCE_EXHAUSTED - if a write lock cannot be obtained
    // StatusCode::CANCELLED otherwise
    //
    //

}

StatusCode DFSClientNodeP2::Fetch(const std::string &filename) {

    ClientContext context;
    dfs_service::FetchResponse response;
    dfs_service::FetchRequest request;

    std::chrono::system_clock::time_point deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout);
    context.set_deadline(deadline);

    if (std::chrono::system_clock::now() > deadline) {
        std::cout << "Deadline exceeded before starting the operation." << std::endl;
    return StatusCode::DEADLINE_EXCEEDED;
    }

    //overwrite the file
    //Open the file to write the content to it
    std::ofstream out_file;
    struct stat fileStat; 
    std::size_t bytes_received = 0;
    std::string WrappedPath = WrapPath(filename);
    std::uint32_t crc = dfs_file_checksum(WrappedPath, &crc_table);

    request.set_filename(filename);

    if (stat(WrappedPath.c_str(), &fileStat) == 0){ 
        request.set_crc(crc);
    }

    std::cout << crc <<std::endl;

    std::unique_ptr<ClientReader<dfs_service::FetchResponse> > reader(service_stub->Fetch(&context, request));

    // Continue reading and writing data for subsequent chunks
    if(reader->Read(&response)){
        if(response.fnf() == 1){
            std::cout << "Server: File NOT FOUND" << std::endl;
            return StatusCode::NOT_FOUND;
        }
        if(response.samecrc() == 1){
            std::cout << "Server: Client_CRC = Server_CRC" << std::endl;
            return StatusCode::ALREADY_EXISTS;
        }
        std::cout << WrappedPath << " Opened" << std::endl;

        out_file.open(WrappedPath, std::ios::binary);
        if (!out_file.is_open()) {
        return StatusCode::CANCELLED;
        }
    }


    while (reader->Read(&response)) {
        out_file.write(response.data().c_str(), response.data().size());
        bytes_received += response.data().size();

        if (std::chrono::system_clock::now() > deadline) {

            std::cout << "Deadline exceeded before starting the operation." << std::endl;
        return StatusCode::DEADLINE_EXCEEDED;
        }
    }

    std::cout << bytes_received << std::endl;
    out_file.close();


    Status status = reader->Finish();

    if (!status.ok()) {
        std::cout << "sometihng not okay" << std::endl;
        // If the RPC failed, return the appropriate StatusCode
        if (status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED) {
            std::cout << "Request timed out. Server may be slow to start or unavailable." << std::endl;
            return StatusCode::DEADLINE_EXCEEDED;
        }else if(status.error_code() == grpc::StatusCode::NOT_FOUND){
            std::remove(WrappedPath.c_str());
            std::cout << "Server File NOT FOUND" << std::endl;
            return StatusCode::NOT_FOUND;
        }
        else{
            std::cout << "RPC Failed with status: " << status.error_message() << std::endl;
            return StatusCode::CANCELLED;
        }
    }

    std::cout << "Fetch complete" << std::endl;

    return StatusCode::OK;



    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to fetch a file here. This method should
    // connect to your gRPC service implementation method
    // that can accept a file request and return the contents
    // of a file from the service.
    //
    // As with the store function, you'll need to stream the
    // contents, so consider the use of gRPC's ClientReader.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // ALREADY_EXISTS - if the local cached file has not changed from the server version
    // StatusCode::NOT_FOUND - if the file cannot be found on the server
    // StatusCode::CANCELLED otherwise
    //
    //
}

grpc::StatusCode DFSClientNodeP2::Delete(const std::string &filename) {

    ClientContext context;
    dfs_service::ResponseStatus stat;
    dfs_service::DeleteRequest request;

    //set deadline
    std::chrono::system_clock::time_point deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout);
    context.set_deadline(deadline);


    /* try to gain write access */
    StatusCode write_lock = RequestWriteAccess(filename);

    if(write_lock != grpc::StatusCode::OK) {
        return write_lock;
    }

    /*delete file from server if write access is gained*/
    request.set_filename(filename);

    Status status = service_stub->Delete(&context, request, &stat);

    StatusCode write_unlock = UnlockWriteAccess(filename);
    if(write_unlock != grpc::StatusCode::OK) {
        return write_lock;
    }

    if (!status.ok()) {
        // If the RPC failed, return the appropriate StatusCode
        if (status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED) {
            std::cout << "Request timed out. Server may be slow to start or unavailable." << std::endl;
            return StatusCode::DEADLINE_EXCEEDED;
        }else if(status.error_code() == grpc::StatusCode::NOT_FOUND){
            std::cout << "Server File NOT FOUND" << std::endl;
            return StatusCode::NOT_FOUND;
        }
        else{
            std::cout << "RPC Failed with status: " << status.error_message() << std::endl;
            return StatusCode::CANCELLED;
        }
    }

    std::cout<< "Delete Completed" << std::endl;

    return StatusCode::OK;

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to delete a file here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // You will also need to add a request for a write lock before attempting to delete.
    //
    // If the write lock request fails, you should return a status of RESOURCE_EXHAUSTED
    // and cancel the current operation.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::RESOURCE_EXHAUSTED - if a write lock cannot be obtained
    // StatusCode::CANCELLED otherwise
    //
    //

}

grpc::StatusCode DFSClientNodeP2::List(std::map<std::string,int>* file_map, bool display) {

    ClientContext context;
    dfs_service::ListResponse list;
    dfs_service::ResponseStatus stat;

    std::chrono::system_clock::time_point deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout);
    context.set_deadline(deadline);

    Status status = service_stub->List(&context, stat, &list);

    //add to file_map

    if (!status.ok()) {
        // If the RPC failed, return the appropriate StatusCode
        if (status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED) {
            std::cout << "Request timed out. Server may be slow to start or unavailable." << std::endl;
            return StatusCode::DEADLINE_EXCEEDED;
        }
        else{
            std::cout << "RPC Failed with status: " << status.error_message() << std::endl;
            return StatusCode::CANCELLED;
        }
    }

       for (const auto& entry : list.listmap()) {
            std::string filename = entry.first;  // File name
            const std::string& mtimeBytes = entry.second;  // mtime bytes

            // Deserialize the mtime (int64_t)
            int64_t mtime;
            memcpy(&mtime, mtimeBytes.data(), sizeof(mtime));

            // Store filename and mtime in the file_map
            (*file_map)[filename] = static_cast<int>(mtime);

            // Optionally, print the decoded file information
            if (display) {
                std::cout << "File: " << filename
                        << ", Modified: " << std::ctime(reinterpret_cast<time_t*>(&mtime)) << std::endl;
            }
        }

    return StatusCode::OK;
    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to list files here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // You can start with your Part 1 implementation and add any additional
    // listing details that would be useful to your solution to the list response.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::CANCELLED otherwise
    //
    //
}

grpc::StatusCode DFSClientNodeP2::Stat(const std::string &filename, void* file_status) {

    ClientContext context;
    dfs_service::StatusRequest request;
    dfs_service::FileStatus stat;

    std::chrono::system_clock::time_point deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout);
    context.set_deadline(deadline);

    request.set_filename(filename);

    Status status = service_stub->Stat(&context, request, &stat);

    if (!status.ok()) {
        // If the RPC failed, return the appropriate StatusCode
        if (status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED) {
            std::cout << "Request timed out. Server may be slow to start or unavailable." << std::endl;
            return StatusCode::DEADLINE_EXCEEDED;
        }else if(status.error_code() == grpc::StatusCode::NOT_FOUND){
            std::cout << "Server File NOT FOUND" << std::endl;
            return StatusCode::NOT_FOUND;
        }
        else{
            std::cout << "RPC Failed with status: " << status.error_message() << std::endl;
            return StatusCode::CANCELLED;
        }
    }

    const std::string& mtimeBytes = stat.modifiedtime();  // mtime bytes

    // Deserialize the mtime (int64_t)
    int64_t mtime;
    memcpy(&mtime, mtimeBytes.data(), sizeof(mtime));

    if (file_status) {
        *reinterpret_cast<int64_t*>(file_status) = mtime;
    }

    std::cout << "File: " << stat.filename() << ", Modified: " << mtime << std::endl;;

    return StatusCode::OK;

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to get the status of a file here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // You can start with your Part 1 implementation and add any additional
    // status details that would be useful to your solution.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::NOT_FOUND - if the file cannot be found on the server
    // StatusCode::CANCELLED otherwise
    //
    //
}

void DFSClientNodeP2::InotifyWatcherCallback(std::function<void()> callback) {

    //
    // STUDENT INSTRUCTION:
    //s
    // This method gets called each time inotify signals a change
    // to a file on the file system. That is every time a file is
    // modified or created.
    //
    // You may want to consider how this section will affect
    // concurrent actions between the inotify watcher and the
    // asynchronous callbacks associated with the server.
    //
    // The callback method shown must be called here, but you may surround it with
    // whatever structures you feel are necessary to ensure proper coordination
    // between the async and watcher threads.
    //
    // Hint: how can you prevent race conditions between this thread and
    // the async thread when a file event has been signaled?
    //
    std::unique_lock<std::mutex> lock(sync_mutex);


    callback();

}

//
// STUDENT INSTRUCTION:
//
// This method handles the gRPC asynchronous callbacks from the server.
// We've provided the base structure for you, but you should review
// the hints provided in the STUDENT INSTRUCTION sections below
// in order to complete this method.
//
void DFSClientNodeP2::HandleCallbackList() {

    void* tag;

    bool ok = false;

    //
    // STUDENT INSTRUCTION:
    //
    // Add your file list synchronization code here.
    //
    // When the server responds to an asynchronous request for the CallbackList,
    // this method is called. You should then synchronize the
    // files between the server and the client based on the goals
    // described in the readme.
    //
    // In addition to synchronizing the files, you'll also need to ensure
    // that the async thread and the file watcher thread are cooperating. These
    // two threads could easily get into a race condition where both are trying
    // to write or fetch over top of each other. So, you'll need to determine
    // what type of locking/guarding is necessary to ensure the threads are
    // properly coordinated.
    //

    // Block until the next result is available in the completion queue.
    while (completion_queue.Next(&tag, &ok)) {
        {

             std::unique_lock<std::mutex> lock(sync_mutex);
            //
            // STUDENT INSTRUCTION:
            //
            // Consider adding a critical section or RAII style lock here
            //

            // The tag is the memory location of the call_data object
            AsyncClientData<FileListResponseType> *call_data = static_cast<AsyncClientData<FileListResponseType> *>(tag);

            dfs_log(LL_DEBUG2) << "Received completion queue callback";

            // Verify that the request was completed successfully. Note that "ok"
            // corresponds solely to the request for updates introduced by Finish().
            // GPR_ASSERT(ok);
            if (!ok) {
                dfs_log(LL_ERROR) << "Completion queue callback not ok.";
            }

            if (ok && call_data->status.ok()) {

            // const auto& file_list = call_data->reply;

            // dfs_log(LL_DEBUG3) << "Handling async callback ";

            // std::map<std::string, int64_t> server_files;

            // for (const auto& entry : call_data->reply.listmap()) {
            //     std::string filename = entry.first;
            //     int64_t mtime;
            //     memcpy(&mtime, entry.second.data(), sizeof(mtime));
            //     server_files[filename] = mtime;

            //     std::cout << "File: " << filename
            //               << ", Modified: " << std::ctime(reinterpret_cast<time_t*>(&mtime)) << std::endl;
            // }

                //
                // STUDENT INSTRUCTION:
                //
                // Add your handling of the asynchronous event calls here.
                // For example, based on the file listing returned from the server,
                // how should the client respond to this updated information?
                // Should it retrieve an updated version of the file?
                // Send an update to the server?
                // Do nothing?
                //



            } else {
                dfs_log(LL_ERROR) << "Status was not ok. Will try again in " << DFS_RESET_TIMEOUT << " milliseconds.";
                dfs_log(LL_ERROR) << call_data->status.error_message();
                std::this_thread::sleep_for(std::chrono::milliseconds(DFS_RESET_TIMEOUT));
            }

            // Once we're complete, deallocate the call_data object.
            delete call_data;

            //
            // STUDENT INSTRUCTION:
            //
            // Add any additional syncing/locking mechanisms you may need here

        }


        // Start the process over and wait for the next callback response
        sleep(3);
        dfs_log(LL_DEBUG3) << "Calling InitCallbackList";
        InitCallbackList();

    }
}

/**
 * This method will start the callback request to the server, requesting
 * an update whenever the server sees that files have been modified.
 *
 * We're making use of a template function here, so that we can keep some
 * of the more intricate workings of the async process out of the way, and
 * give you a chance to focus more on the project's requirements.
 */
void DFSClientNodeP2::InitCallbackList() {
    CallbackList<FileRequestType, FileListResponseType>();
}

//
// STUDENT INSTRUCTION:
//
// Add any additional code you need to here
//

