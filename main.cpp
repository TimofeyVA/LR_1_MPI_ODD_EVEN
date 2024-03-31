#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <mpi.h>

const int ROOT_NODE = 0;

/* Для qsort */
int compare(const void* a, const void* b)
{
    return (*(int*)a - *(int*)b);
}

int* get_part_of_array(int* arr, int start_pos, int end_pos)
{
    int* part_arr = new int[end_pos - start_pos];
    int j = 0;
    for (int i = start_pos; i < end_pos; i++) {
        part_arr[j] = arr[i];
        j++;
    }
    return part_arr;
}

int* merge_arrays(int* arr1, int* arr2, int block_size)
{
    int* merged_arr = new int[block_size * 2];
    for (int i = 0; i < block_size; i++) {
        merged_arr[i] = arr1[i];
        merged_arr[i + block_size] = arr2[i];
    }
    return merged_arr;
}

int* merge_arrays(int* arr1, int* arr2, int arr1_block_size, int arr2_block_size)
{
    int* merged_arr = new int[arr1_block_size + arr2_block_size];
    int j = 0;
    for (int i = 0; i < arr1_block_size; i++) {
        merged_arr[j] = arr1[i];
        j++;
    }
    for (int i = 0; i < arr2_block_size; i++) {
        merged_arr[j] = arr2[i];
        j++;
    }
    return merged_arr;
}

void print_array(int* array, int length)
{
    for (int i = 0; i < length; i++) {
        printf("A[%d] = %d \n", array[i]);
    }
}

int* get_randomized_array(int size)
{
    int* array = new int[size];
    int range = size;
    for (int i = 0; i < size; i++) {
        array[i] = rand() % range + 1;
    }
    return array;
}

// mpic++ oddeven.cpp -o oddeven
/* Схема:
На ROOT_NODE создается исходный массив, который рассылается по нодам, с размером блока ( размер_массива / количество_нод )
На ROOT_NODE также остается только массив с полученным размером блока.
Потом уже работает алгоритм чет-нечетной перестановки.
Узлы меняются друг с другом массивами.
Сортируются.
Младший нод оставляет себе меньшую часть. Старший большую. */
/* mprun -np PROCESS_COUNT oddeven ARRAY_SIZE */
int main(int argc, char* argv[])
{
    double inittime, totaltime;

    int array_size = atoi(argv[1]);
    int* array = new int[array_size];
    array = get_randomized_array(array_size);

    int* recv_array;
    int* tmp_recv_array;
    int** send_array;
    MPI::Status status;

    MPI::Init(argc, argv);
    inittime = MPI::Wtime();
    int nodes_count = MPI::COMM_WORLD.Get_size();

    if (array_size < nodes_count) {
        MPI::Finalize();
        printf("ERROR!!! Array size should be greater or equal to nodes count! \n ");
        return 0;
    }

    int current_node = MPI::COMM_WORLD.Get_rank();

    if (nodes_count > array_size)
        nodes_count = array_size - 1;
    const int LAST_NODE = nodes_count - 1;

    int block_size = array_size / nodes_count;
    bool diff_slices;
    if (array_size % nodes_count != 0)
        diff_slices = true;
    else
        diff_slices = false;

    printf("current node is %d %d\n", current_node, nodes_count);

    /* В начале ROOT_NODE рассылает всем block_size элементов и у себя оставляет столько же */
    /* Если нельзя распределить исходный массив всем узлам равномерно( размер блока получается нецелым ), то */
    /* остаток + размер блока отправляем последнему узлу */

    /* Для всех узлов кроме ROOT и LAST */
    if (current_node != ROOT_NODE && current_node != LAST_NODE) {
        recv_array = new int[block_size];
        MPI::COMM_WORLD.Recv(recv_array, block_size, MPI::INT, ROOT_NODE, MPI::ANY_TAG, status);
    }
    /* Для LAST_NODE */
    else if (current_node == LAST_NODE && current_node != ROOT_NODE) {
        if (diff_slices) {
            recv_array = new int[block_size + array_size % nodes_count];
            MPI::COMM_WORLD.Recv(recv_array, block_size + array_size % nodes_count, MPI::INT, ROOT_NODE, MPI::ANY_TAG, status);
        } else {
            recv_array = new int[block_size];
            MPI::COMM_WORLD.Recv(recv_array, block_size, MPI::INT, ROOT_NODE, MPI::ANY_TAG, status);
        }
    }
    /* Для ROOT_NODE */
    else {
        /* Делим массив на блоки для отправки */
        send_array = new int*[nodes_count];
        for (int i = ROOT_NODE; i < LAST_NODE; i++)
            send_array[i] = new int[block_size];

        if (diff_slices)
            send_array[LAST_NODE] = new int[block_size + array_size % nodes_count];
        else
            send_array[LAST_NODE] = new int[block_size];

        for (int i = ROOT_NODE; i < LAST_NODE; i++)
            for (int j = 0; j < block_size; j++)
                send_array[i][j] = array[i * block_size + j];

        if (diff_slices)
            for (int j = 0; j < block_size + array_size % nodes_count; j++)
                send_array[LAST_NODE][j] = array[block_size * (LAST_NODE) + j];
        else
            for (int j = 0; j < block_size; j++)
                send_array[LAST_NODE][j] = array[(LAST_NODE)*block_size + j];
        /* ----- */

        if (nodes_count != 1) {
            for (int i = 1; i < LAST_NODE; i++) {
                MPI::COMM_WORLD.Send(send_array[i], block_size, MPI::INT, i, 0);
            }
            if (diff_slices)
                MPI::COMM_WORLD.Send(send_array[LAST_NODE], block_size + array_size % nodes_count, MPI::INT, LAST_NODE, 0);
            else
                MPI::COMM_WORLD.Send(send_array[LAST_NODE], block_size, MPI::INT, LAST_NODE, 0);
        }

        recv_array = new int[block_size];
        recv_array = send_array[0];
    }

    /* Ждем пока каждый нод получит свою долю */
    MPI::COMM_WORLD.Barrier();

    for (int j = 0; j < nodes_count; j++) {
        MPI::COMM_WORLD.Barrier();
        if (j == current_node)
            if (diff_slices && current_node == LAST_NODE) {
                printf("node[%d] count %d\n", current_node, block_size + array_size % nodes_count);
                for (int i = 0; i < block_size + array_size % nodes_count; i++)
                    printf("node[%d] array[%d] is %d \n", current_node, i, recv_array[i]);
            } else {
                for (int i = 0; i < block_size; i++)
                    printf("node[%d] array[%d] is %d \n", current_node, i, recv_array[i]);
            }
    }

    MPI::COMM_WORLD.Barrier();

    if (nodes_count == 1)
        qsort(recv_array, array_size, sizeof(int), compare);

    /* Алгоритм чет-нечетной перестановки */
    /* Обмен массивами между нодами, слияния, сортировка и отсечение */
    for (int n = 0; n < nodes_count; n++) {
        /* Четная итерация */
        if (n % 2 == 0) {
            for (int j = 0; j < LAST_NODE; j += 2) {
                if (current_node == j) {
                    /* Отправление соседней ноде своего массива */
                    MPI::COMM_WORLD.Send(recv_array, block_size, MPI::INT, j + 1, 0);
                    /* Если есть блок с остатком и соседний нод последний */
                    /* Если это не так, то в текущем ноде всегда будет количество элементов без остатка */
                    if (diff_slices && j + 1 == LAST_NODE) {
                        tmp_recv_array = new int[block_size + array_size % nodes_count];
                        MPI::COMM_WORLD.Recv(tmp_recv_array, block_size + array_size % nodes_count, MPI::INT, j + 1, MPI::ANY_TAG, status);
                        int* merged_array = merge_arrays(recv_array, tmp_recv_array, block_size, block_size + array_size % nodes_count);
                        qsort(merged_array, block_size + block_size + array_size % nodes_count, sizeof(int), compare);
                        recv_array = get_part_of_array(merged_array, 0, block_size);
                    } else {
                        tmp_recv_array = new int[block_size];
                        MPI::COMM_WORLD.Recv(tmp_recv_array, block_size, MPI::INT, j + 1, MPI::ANY_TAG, status);
                        int* merged_array = merge_arrays(recv_array, tmp_recv_array, block_size);
                        qsort(merged_array, block_size * 2, sizeof(int), compare);
                        recv_array = get_part_of_array(merged_array, 0, block_size);
                    }
                } else if (current_node == j + 1) {
                    tmp_recv_array = new int[block_size];
                    /* Если текущий нод последний и есть остаток, то отправляем соседу массив с размером блока с остатком */
                    if (diff_slices && current_node == LAST_NODE)
                        MPI::COMM_WORLD.Send(recv_array, block_size + array_size % nodes_count, MPI::INT, j, 0);
                    else
                        MPI::COMM_WORLD.Send(recv_array, block_size, MPI::INT, j, 0);

                    MPI::COMM_WORLD.Recv(tmp_recv_array, block_size, MPI::INT, j, MPI::ANY_TAG, status);
                    if (diff_slices && current_node == LAST_NODE) {
                        int* merged_array = merge_arrays(recv_array, tmp_recv_array, block_size + array_size % nodes_count, block_size);
                        qsort(merged_array, block_size * 2 + array_size % nodes_count, sizeof(int), compare);
                        recv_array = get_part_of_array(merged_array, block_size, 2 * block_size + array_size % nodes_count);
                    } else {
                        int* merged_array = merge_arrays(recv_array, tmp_recv_array, block_size);
                        qsort(merged_array, block_size * 2, sizeof(int), compare);
                        recv_array = get_part_of_array(merged_array, block_size, 2 * block_size);
                    }
                }
            }
        } else {
            for (int j = 1; j < LAST_NODE; j += 2) {
                if (current_node == j) {
                    MPI::COMM_WORLD.Send(recv_array, block_size, MPI::INT, j + 1, 0);
                    if (diff_slices && j + 1 == LAST_NODE) {
                        tmp_recv_array = new int[block_size + array_size % nodes_count];
                        MPI::COMM_WORLD.Recv(tmp_recv_array, block_size + array_size % nodes_count, MPI::INT, j + 1, MPI::ANY_TAG, status);
                        int* merged_array = merge_arrays(recv_array, tmp_recv_array, block_size, block_size + array_size % nodes_count);
                        qsort(merged_array, block_size * 2 + array_size % nodes_count, sizeof(int), compare);
                        recv_array = get_part_of_array(merged_array, 0, block_size);
                    } else {
                        tmp_recv_array = new int[block_size];
                        MPI::COMM_WORLD.Recv(tmp_recv_array, block_size, MPI::INT, j + 1, MPI::ANY_TAG, status);
                        int* merged_array = merge_arrays(recv_array, tmp_recv_array, block_size);
                        qsort(merged_array, block_size * 2, sizeof(int), compare);
                        recv_array = get_part_of_array(merged_array, 0, block_size);
                    }
                } else if (current_node == j + 1) {
                    tmp_recv_array = new int[block_size];
                    if (diff_slices && current_node == LAST_NODE)
                        MPI::COMM_WORLD.Send(recv_array, block_size + array_size % nodes_count, MPI::INT, j, 0);
                    else
                        MPI::COMM_WORLD.Send(recv_array, block_size, MPI::INT, j, 0);

                    MPI::COMM_WORLD.Recv(tmp_recv_array, block_size, MPI::INT, j, MPI::ANY_TAG, status);
                    if (diff_slices && current_node == LAST_NODE) {
                        int* merged_array = merge_arrays(recv_array, tmp_recv_array, block_size + array_size % nodes_count, block_size);
                        qsort(merged_array, block_size * 2 + array_size % nodes_count, sizeof(int), compare);
                        recv_array = get_part_of_array(merged_array, block_size, 2 * block_size + array_size % nodes_count);
                    } else {
                        int* merged_array = merge_arrays(recv_array, tmp_recv_array, block_size);
                        qsort(merged_array, block_size * 2, sizeof(int), compare);
                        recv_array = get_part_of_array(merged_array, block_size, 2 * block_size);
                    }
                }
            }
        }
    }

    MPI::COMM_WORLD.Barrier();

    for (int j = 0; j < nodes_count; j++) {
        MPI::COMM_WORLD.Barrier();
        if (j == current_node)
            if (diff_slices && current_node == LAST_NODE) {
                printf(" count %d\n", block_size + array_size % nodes_count);
                for (int i = 0; i < block_size + array_size % nodes_count; i++)
                    printf("node[%d] Sorted array[%d] is %d \n", current_node, i, recv_array[i]);
            } else {
                for (int i = 0; i < block_size; i++) {
                    printf("node[%d] Sorted array[%d] is %d \n", current_node, i, recv_array[i]);
                }
            }
    }

    MPI::COMM_WORLD.Barrier();
    totaltime = MPI::Wtime() - inittime;
    printf(" Communication time of %d: %f seconds\n\n", current_node, totaltime);
    MPI::Finalize();
    return 0;
}
