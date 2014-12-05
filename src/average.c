#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>
#include <stdbool.h>

/* Quelques structures utiles */
typedef struct coordinates {
    int x;
    int y;
} coordinates;

coordinates coordinates_init(int x, int y) {
    coordinates coord;
    coord.x = x;
    coord.y = y;
    return coord;
}

bool coordinates_equals(coordinates a, coordinates b) {
    return a.x == b.x && a.y == b.y;
}

typedef struct matrix {
    coordinates size;
    double* data;
} matrix;

matrix matrix_init(coordinates size) {
    matrix matrix;
    unsigned long matrix_size = (unsigned long) (size.x * size.y);
    matrix.size = size;
    matrix.data = malloc(sizeof(double) * matrix_size);
    for(unsigned long i = 0; i < matrix_size; i++) {
        matrix.data[i] = 0;
    }
    return matrix;
}

double matrix_get_case(matrix matrix, coordinates coord) {
    return matrix.data[coord.x * matrix.size.y + coord.y];
}

void matrix_set_case(matrix* matrix, double value, coordinates coord) {
    matrix->data[coord.x * matrix->size.y + coord.y] = value;
}

void matrix_destruct(matrix* matrix) {
    free(matrix->data);
}

typedef struct environment {
    double p;
    int t;
    matrix matrix;
} environment;


/* Input parsing */
environment parse_file_header() {
    environment data;
    coordinates matrix_size;

    if(scanf("%d %d %lf %d", &matrix_size.y, &matrix_size.x, &data.p, &data.t) != 4) {
        exit(EXIT_FAILURE);
    }
    data.matrix = matrix_init(matrix_size);

    return data;
}

coordinates parse_entry_until_request(environment* environment, bool update_matrix) {
    coordinates coord;
    int type;
    double x;

    while(scanf("%d %d %d %lf", &type, &coord.x, &coord.y, &x) == 4) {
        switch(type) {
            case 0:
                if(update_matrix) {
                    matrix_set_case(&environment->matrix, x, coord);
                }
                break;
            case 2:
                return coord;
            default:
                fprintf(stderr, "Unknown description type %d\n", type);
        }
    }

    return coordinates_init(-1, -1);
}

/* Useful functions */
int get_my_id() {
    int my_id;
    MPI_Comm_rank(MPI_COMM_WORLD, &my_id);
    return my_id;
}

int get_number_of_cpu() {
    int number;
    MPI_Comm_size(MPI_COMM_WORLD, &number);
    return number;
}

coordinates get_my_cell_coordinates(coordinates grid_size) {
    int my_id = get_my_id();

    return coordinates_init(my_id / grid_size.y, my_id % grid_size.y);
}

int mod(int val, int mod) {
    while(val < 0) {
        val += mod;
    }
    return val % mod;
}

int cpu_id_from_coordinates_with_mod(int x, int y, coordinates grid_size) {
    return mod(x, grid_size.x) * grid_size.y + mod(y, grid_size.y);
}

//do variable move in the matrix. from is the pid from which we shoud get data and to the pid to which we shoud send val
double move_double(int from, int to, double val) {
    MPI_Send(&val, 1, MPI_DOUBLE, to, 0, MPI_COMM_WORLD);
    MPI_Recv(&val, 1, MPI_DOUBLE, from, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    return val;
}

/* Main code */
int main(int argc, char* argv[])
{
    MPI_Init(&argc, &argv);

    int my_id = get_my_id();
    environment environment;
    double my_value;
    int number_of_cpu = get_number_of_cpu();
    coordinates target, my_coordinates;

    if(my_id == 0) {
        environment = parse_file_header();
        if(number_of_cpu != environment.matrix.size.x * environment.matrix.size.y) {
            fprintf(stderr, "The number of CPU is different than the size of the matrix.\n");
            exit(EXIT_FAILURE);
        }
        target = parse_entry_until_request(&environment, true);
    }

    MPI_Bcast(&environment.p, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    MPI_Bcast(&environment.t, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&environment.matrix.size.x, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&environment.matrix.size.y, 1, MPI_INT, 0, MPI_COMM_WORLD);
    my_coordinates = get_my_cell_coordinates(environment.matrix.size);

    //broadcast all data to process
    if(my_id == 0) {
        for(int i = 0; i < number_of_cpu; i++) {
            MPI_Send(&environment.matrix.data[i], 1, MPI_DOUBLE, i, 0, MPI_COMM_WORLD);
        }
    }
    MPI_Recv(&my_value, 1, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    //do computation
    for(int i = 0; i < environment.t; i++) {
        double b, d, f, h;

        b = move_double(
                        cpu_id_from_coordinates_with_mod(my_coordinates.x - 1, my_coordinates.y, environment.matrix.size),
                        cpu_id_from_coordinates_with_mod(my_coordinates.x + 1, my_coordinates.y, environment.matrix.size),
                        my_value
                        );
        d = move_double(
                        cpu_id_from_coordinates_with_mod(my_coordinates.x, my_coordinates.y - 1, environment.matrix.size),
                        cpu_id_from_coordinates_with_mod(my_coordinates.x, my_coordinates.y + 1, environment.matrix.size),
                        my_value
                        );
        f = move_double(
                        cpu_id_from_coordinates_with_mod(my_coordinates.x, my_coordinates.y + 1, environment.matrix.size),
                        cpu_id_from_coordinates_with_mod(my_coordinates.x, my_coordinates.y - 1, environment.matrix.size),
                        my_value
                        );
        h = move_double(
                        cpu_id_from_coordinates_with_mod(my_coordinates.x + 1, my_coordinates.y, environment.matrix.size),
                        cpu_id_from_coordinates_with_mod(my_coordinates.x - 1, my_coordinates.y, environment.matrix.size),
                        my_value
                        );

        my_value = (1 - environment.p) * my_value + environment.p * (b + d + f + h) / 4;

        if(my_id == 0 && i % 100 == 99) {
            printf("Iteration %d\n", i + 1);
        }
    }

    //retrieve back all data
    MPI_Send(&my_value, 1, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD);
    if(my_id == 0) {
        for(int i = 0; i < number_of_cpu; i++) {
            MPI_Recv(&environment.matrix.data[i], 1, MPI_DOUBLE, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
    }

    if(my_id == 0) {
        coordinates end_target = coordinates_init(-1, -1);
        while(!coordinates_equals(target, end_target)) {
            printf("Value of case (%d, %d) is %lf.\n", target.x, target.y, matrix_get_case(environment.matrix, target));
            target = parse_entry_until_request(&environment, false);
        };
    }

    MPI_Finalize();
    return EXIT_SUCCESS;
}
