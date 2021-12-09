#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <fstream>
#include <ctime>
extern "C" {
    #include "tasks.h"
    #include "utils.h"
}

void sendMapTasks(int, int, int, char*);
void collectAnswerAndOutput(int, int, char*, MPI_Datatype);
void terminateMapTasks(int);
void terminateRedTasks(int, int);
char* getNextFile(char*);


#define POSTFIX_LENGTH 6
#define MAX_SIZE 10 * (2 << 20)
#define TERMINATION_TAG 110398

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int world_size, rank;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);


    //Structure of KeyValue is constant hence we can define a custom type for it

    /* create a type for struct car */
    const int nitems=2;
    int          blocklengths[2] = {8,1};
    MPI_Datatype types[2] = {MPI_CHAR, MPI_INT};
    MPI_Datatype mpi_keyvalue_type;
    MPI_Aint     offsets[2];

    offsets[0] = offsetof(KeyValue, key);
    offsets[1] = offsetof(KeyValue, val);

    MPI_Type_create_struct(nitems, blocklengths, offsets, types, &mpi_keyvalue_type);
    MPI_Type_commit(&mpi_keyvalue_type);

    // Get command-line params
    char *input_files_dir = argv[1];
    int num_files = atoi(argv[2]);
    int num_map_workers = atoi(argv[3]);
    int num_reduce_workers = atoi(argv[4]);
    char *output_file_name = argv[5];
    int map_reduce_task_num = atoi(argv[6]);

    // Identify the specific map function to use
    MapTaskOutput* (*map) (char*);
    switch(map_reduce_task_num){
        case 1:
            map = &map1;
            break;
        case 2:
            map = &map2;
            break;
        case 3:
            map = &map3;
            break;
    }

    // Distinguish between master, map workers and reduce workers
    if (rank == 0) {
        // This is the master process
        sendMapTasks(num_files, num_map_workers, num_reduce_workers, input_files_dir);
        terminateMapTasks(num_map_workers);
        terminateRedTasks(num_map_workers, num_reduce_workers);
        collectAnswerAndOutput(num_map_workers, num_reduce_workers, output_file_name, mpi_keyvalue_type);
    } else if ((rank >= 1) && (rank <= num_map_workers)) {
        // This is a map worker process
        char* buffer = new char[MAX_SIZE];
        MPI_Status Stat;
        int rc;

        //Await packet from master
        rc = MPI_Recv(buffer, MAX_SIZE, MPI_CHAR, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &Stat);

        while (Stat.MPI_TAG != TERMINATION_TAG) {
            //Process the input given
            MapTaskOutput* output = (*map)(buffer);
            for (int i = 0; i < output->len; i++) {
                //Calculate partition value belongs to
                int partitionValue = partition(output->kvs[i].key, num_reduce_workers);
                //Send processed value to correct reduce worker
                MPI_Send(&(output->kvs[i]), 1, mpi_keyvalue_type, partitionValue + num_map_workers + 1, rank, MPI_COMM_WORLD);
                //Acknowledgement from Red worker
                rc = MPI_Recv(NULL, 0, MPI_CHAR, partitionValue + num_map_workers + 1, MPI_ANY_TAG, MPI_COMM_WORLD, &Stat);
            }
            //Reset buffer
            memset( buffer, '\0', sizeof(char)*MAX_SIZE );
            rc = MPI_Send(NULL, 0, MPI_CHAR, 0, rank, MPI_COMM_WORLD);
            free_map_task_output(output);
            //Next packet from Master
            rc = MPI_Recv(buffer, MAX_SIZE, MPI_CHAR, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &Stat);
        }

        //Acknowledge Termination
        MPI_Send(NULL, 0, MPI_CHAR, 0 , TERMINATION_TAG, MPI_COMM_WORLD);

    } else {
        // This is a reduce worker process
        int rc;
        MPI_Status Stat;
        KeyValue recv;
        std::unordered_map<std::string, std::vector<int>> resultsMap;

        //Get packet from anyone
        rc = MPI_Recv(&recv, 1, mpi_keyvalue_type, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &Stat);
        while (Stat.MPI_TAG != TERMINATION_TAG) {
            //Adding values to map
            std::string s = recv.key;
            int val = recv.val;
            if (resultsMap.find(s) == resultsMap.end()) {
                std::vector<int> tempArr;
                resultsMap.insert({ s, tempArr });
            }
            std::vector<int> innerArr = resultsMap[s];
            innerArr.push_back(val);
            resultsMap[s] = innerArr;
            MPI_Send(NULL, 0, MPI_CHAR, Stat.MPI_SOURCE, rank, MPI_COMM_WORLD);
            rc = MPI_Recv(&recv,1, mpi_keyvalue_type, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &Stat);
        }

        //Acknowledge Termination
        MPI_Send(NULL, 0, MPI_CHAR, 0 , TERMINATION_TAG, MPI_COMM_WORLD);


        if (resultsMap.size() != 0) {

            for (auto const& x : resultsMap)
            {
                int arr[x.second.size()];
                std::copy(x.second.begin(), x.second.end(), arr);     
                KeyValue kv = reduce((char *) x.first.c_str(), arr, x.second.size());
                MPI_Send(&kv, 1, mpi_keyvalue_type, 0, rank, MPI_COMM_WORLD);
            }
            
        }

        //Let master know this reduce worker is done
        MPI_Send(NULL, 0, mpi_keyvalue_type, 0, TERMINATION_TAG, MPI_COMM_WORLD);
    }

    //Clean up
    MPI_Finalize();
    return 0;
}

void sendMapTasks(int num_files, int num_map_workers, int num_reduce_workers, char* input_files_dir) {
    int num_files_left = num_files;
    int last_worker = 1;
    int rc;
    MPI_Status Stat;
    MPI_Request reqs[num_map_workers];
	MPI_Status stats[num_map_workers];
    int tasks_completed = 0;
    char* files[num_files];

    for (int i = last_worker; i <= num_map_workers; i++) {
            if (num_files_left <= 0) continue;

            //Make String Path
            /*
                Should be able to rework this area now that we can use cpp
            */
            char *index;
            asprintf(&index, "%d", num_files_left - 1);
            int lengthOfPath = strlen(input_files_dir) + strlen(index) + POSTFIX_LENGTH;
            char* dirPath = (char*) malloc(lengthOfPath);
            dirPath[0] = '\0';
            strcat(dirPath, input_files_dir);
            strcat(dirPath, "/");
            strcat(dirPath, index);
            strcat(dirPath, ".txt");

            //Get File from Path
            files[num_files_left - 1] = getNextFile(dirPath);
            rc = MPI_Isend(files[num_files_left - 1], strlen(files[num_files_left - 1]), MPI_CHAR, i, i, MPI_COMM_WORLD, &reqs[i - 1]);            
            num_files_left--;
            free(dirPath);
    }

    while (tasks_completed < num_files) {

        rc = MPI_Recv(NULL, 0, MPI_CHAR, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &Stat);
        tasks_completed++;

        if (num_files_left > 0) {

            //Make String Path
            /*
                Should be able to rework this area now that we can use cpp
            */
            char *index;
            asprintf(&index, "%d", num_files_left - 1);
            int lengthOfPath = strlen(input_files_dir) + strlen(index) + POSTFIX_LENGTH;
            char* dirPath = (char*) malloc(lengthOfPath);
            dirPath[0] = '\0';
            strcat(dirPath, input_files_dir);
            strcat(dirPath, "/");
            strcat(dirPath, index);
            strcat(dirPath, ".txt");

            //Get File from Path
            files[num_files_left - 1] = getNextFile(dirPath);
            rc = MPI_Isend(files[num_files_left - 1], strlen(files[num_files_left - 1]), MPI_CHAR, Stat.MPI_SOURCE, Stat.MPI_SOURCE, MPI_COMM_WORLD, &reqs[Stat.MPI_SOURCE - 1]);
            
            num_files_left--;
            free(dirPath);
        }

    }

    for (int i = 0; i < num_files; i++) {
        free(files[i]);
    }
}

char* getNextFile(char* path) {
    char* buffer = 0;
    long length;
    FILE * f = fopen (path, "rb"); 

    if (f)
    {
      fseek (f, 0, SEEK_END);
      length = ftell (f);
      fseek (f, 0, SEEK_SET);
      buffer = (char*)malloc ((length+1)*sizeof(char));
      if (buffer)
      {
        fread (buffer, sizeof(char), length, f);
      }
      fclose (f);
    }
    buffer[length] = '\0';

    return buffer;
}


void terminateMapTasks(int num_map_workers) {
    int rc;
    MPI_Request reqs[num_map_workers];
	MPI_Status stats[num_map_workers];
    for (int i = 1; i <= num_map_workers; i++) {
        MPI_Isend(NULL, 0, MPI_CHAR, i, TERMINATION_TAG, MPI_COMM_WORLD, &reqs[i - 1]);
        MPI_Irecv(NULL, 0, MPI_CHAR, i, TERMINATION_TAG, MPI_COMM_WORLD, &reqs[i - 1]);
    }
    MPI_Waitall(num_map_workers, reqs, stats);
}

void terminateRedTasks(int num_map_workers, int num_reduce_workers) {
    int rc;
    MPI_Request reqs[num_reduce_workers];
	MPI_Status stats[num_reduce_workers];
    for (int i = num_map_workers + 1; i <= num_map_workers + num_reduce_workers; i++) {
        MPI_Isend(NULL, 0, MPI_CHAR, i, TERMINATION_TAG, MPI_COMM_WORLD, &reqs[i - (num_map_workers + 1)]);
        MPI_Irecv(NULL, 0, MPI_CHAR, i, TERMINATION_TAG, MPI_COMM_WORLD, &reqs[i - (num_map_workers + 1)]);
    }
    MPI_Waitall(num_reduce_workers, reqs, stats);
}


void collectAnswerAndOutput(int num_map_workers, int num_reduce_workers, char* output_file_name, MPI_Datatype mpi_keyvalue_type) {
    int rc;
    MPI_Status Stat;
    KeyValue recv;
    std::ofstream myfile;
    myfile.open(output_file_name);
    // printf("Collecting results\n");
    for (int i = 0; i < num_reduce_workers; i++) {
        rc = MPI_Recv(&recv, 1, mpi_keyvalue_type, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &Stat);
        while (Stat.MPI_TAG != TERMINATION_TAG) {
            myfile << recv.key << " " << recv.val << "\n";
            rc = MPI_Recv(&recv, 1, mpi_keyvalue_type, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &Stat);
        }
    }

    // printf("Collecting results done\n");

    myfile.close();
}


