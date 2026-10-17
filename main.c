#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>

////////////////////////////////////////////////////// Storage header and Structs and Enums /////////////////////////////////////////////////////////////////

#define max_username_length 32
#define max_email_length 255


#define page_size 4096

#define size_of_id (int)sizeof(((Row *)0)->id)
#define size_of_username (int)sizeof(((Row *)0)->username)
#define size_of_email (int)sizeof(((Row *)0)->email)

#define offset_of_id 0
#define offset_of_username (size_of_id)
#define offset_of_email (offset_of_username + size_of_username)

#define row_size (size_of_id + size_of_email + size_of_username)
#define max_rows_in_page (page_size / row_size)

//assuming only 100 pages in tables
#define max_pages_in_table 100
#define max_filename_size 100


typedef struct
{
    void* cache_of_pages[max_pages_in_table];
    FILE* file_descriptor;
    char* filename;
} Pager;


typedef struct
{
    int num_rows;
    Pager pager;
    // void* pages[max_pages_in_table];
} Table;


typedef enum
{
    META_COMMAND_SUCCESS,
    META_COMMAND_UNRECOGNIZED_COMMAND
} MetaCommandResult;

typedef enum
{
    PREPARE_SUCCESS,
    PREPARE_UNRECOGNIZED_STATEMENT,
    PREPARE_SYNTAX_ERROR,
    PREPARE_STRING_TOO_LONG,
    PREPARE_TOO_MANY_ARGUMENTS,
    PREPARE_TOO_FEW_ARGUMENTS,
    PREPARE_NEGATIVE_ID
} PreparedStatementResult;

typedef enum
{
    STATEMENT_INSERT, 
    STATEMENT_SELECT
} StatementType;

typedef struct
{
    uint32_t id;
    char username[max_username_length + 1];
    char email[max_email_length + 1];
} Row;


typedef struct
{
    StatementType type;
    Row Row_data_when_insert;
} Statement;

typedef enum
{
    EXECUTE_SUCCESS,
    EXECUTE_TABLE_FULL
} ExecuteResult;


typedef struct
{
    char* buffer;
    size_t buffer_length;
    ssize_t input_length;
} InputBuffer;

long get_file_size(FILE *file) 
{
    if (file == NULL) 
    return 0;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    return size;
}

Table* table_initializer(char* filename)
{
    // 1. Safety check for malloc
    Table* table = (Table *)malloc(sizeof(Table));
    if (table == NULL)
    return NULL;

    table->pager.filename = (char *)malloc(max_filename_size * sizeof(char));
    if (table->pager.filename == NULL) 
    {
        free(table);
        return NULL;
    }

    strncpy(table->pager.filename, filename, max_filename_size - 1);
    table->pager.filename[max_filename_size - 1] = '\0';

    table->pager.file_descriptor = fopen(table->pager.filename, "rb+");
    if (table->pager.file_descriptor == NULL) 
    {
        table->pager.file_descriptor = fopen(table->pager.filename, "wb+");
        if (table->pager.file_descriptor == NULL) 
        {
            perror("Error creating file");
            free(table->pager.filename);
            free(table);
            exit(EXIT_FAILURE);
        }
    }

    table->num_rows = get_file_size(table->pager.file_descriptor) / row_size;
    for(int i = 0; i < max_pages_in_table; i++)
    {
        //cold start of cache
        table->pager.cache_of_pages[i] = NULL;
    }
    return table;
}


//////////////////////////////////////////////////////// Persistence code ////////////////////////////////////////////////////////


int bring_page_into_memory(int page_index, Table* table, void* page_buffer)
{
    long offset_in_file = page_index * page_size;
    if (offset_in_file > get_file_size(table->pager.file_descriptor))
    return 0;
    
    fseek(table->pager.file_descriptor, offset_in_file, SEEK_SET);

    ssize_t bytes_read = fread(page_buffer, 1, page_size, table->pager.file_descriptor);

    if (ferror(table->pager.file_descriptor)) 
    {
        perror("Error reading from file");
        return 0;
    }
    return 1;
}

int update_file(int page_number, Table* table, size_t size_to_write)
{
    long offset_in_file = page_number * page_size;
    void* offset_in_cache = table->pager.cache_of_pages[page_number];
    if (fseek(table->pager.file_descriptor, offset_in_file, SEEK_SET) != 0) 
    {
        perror("Fseek to file_offset failed");
        return 0;
    }
    size_t bytes_written = fwrite(offset_in_cache, 1, size_to_write, table->pager.file_descriptor);
    if (bytes_written < size_to_write) {
        perror("Error writing page to disk (incomplete write)");
        return 0;
    }
    fflush(table->pager.file_descriptor);
    return 1;
}

///////////////////////////////////////////////////////////////// IO //////////////////////////////////////////////////////////////


InputBuffer* input_buffer_initializer()
{
    InputBuffer* input_buffer = (InputBuffer *)malloc(sizeof(InputBuffer));
    input_buffer->buffer = NULL;
    input_buffer->buffer_length = 0;
    input_buffer->input_length = 0;

    return input_buffer;
}

void print_prompt()
{
    printf("Database > ");
}

void getInput(InputBuffer* input_buffer)
{
    ssize_t size = getline(&(input_buffer->buffer), &(input_buffer->buffer_length), stdin);
    if (size > 0)
    {
        if (input_buffer->buffer[size - 1] == '\n')
        size--;
        input_buffer->input_length = size;
        input_buffer->buffer[size] = 0;
    }
    else
    {
        if (size == -1)
        {
            if (feof(stdin)) 
            {
                printf("\nGoodbye!\n");
                exit(EXIT_SUCCESS);
            }
        }
        printf("Error reading input \n");
        exit(EXIT_FAILURE);
    }
}

void close_input_buffer(InputBuffer* input_buffer)
{
    free(input_buffer->buffer);
    free(input_buffer);
}


/////////////////////////////////////////////////////////////// Storage and insert ////////////////////////////////////////////////////////////



void copy_row_data_from_stmt_to_table(Row* row_data, void* destination)
{
    char* dest = (char*)destination;

    memcpy(dest + offset_of_id, &(row_data->id), size_of_id);
    memcpy(dest + offset_of_username, &(row_data->username), size_of_username);
    memcpy(dest + offset_of_email, &(row_data->email), size_of_email);
}

ExecuteResult execute_insert(Statement* stmt, Table* table)
{
    //if you cant have another row cuz table can have 100 pages and each page has 14? rows
    if (table->num_rows >= max_rows_in_page * max_pages_in_table)
    {
        return EXECUTE_TABLE_FULL;
    }
    else
    {
        //find out teh page index
        int page_index = table->num_rows / max_rows_in_page;
        if (table->pager.cache_of_pages[page_index] == NULL)
        {
            void* page_buffer = calloc(1, page_size);
            bring_page_into_memory(page_index, table, page_buffer);
            table->pager.cache_of_pages[page_index] = page_buffer;
        }
        //find out teh row index within the page and the offset to where we need to copy to.
        int row_offset = table->num_rows % max_rows_in_page;
        uint32_t byte_offset = row_offset * row_size;
        void* destination_pointer = byte_offset + (char *)table->pager.cache_of_pages[page_index];
        copy_row_data_from_stmt_to_table(&(stmt->Row_data_when_insert), destination_pointer);

        table->num_rows++;
        return EXECUTE_SUCCESS;
    }
}


///////////////////////////////////////////////////////// Select ///////////////////////////////////////////////////////////////



void copy_row_data_from_table_to_temp_to_print(Row* temp_to_print, void* source_pointer)
{
    char* src = (char*)source_pointer;

    memcpy(&(temp_to_print->id), src + offset_of_id, size_of_id);
    memcpy(&(temp_to_print->username), src + offset_of_username, size_of_username);
    memcpy(&(temp_to_print->email), src + offset_of_email, size_of_email);
}

void print_row(Row* row) 
{
  printf("(%d, %s, %s)\n", row->id, row->username, row->email);
}

ExecuteResult execute_select(Statement* stmt, Table* table)
{
    Row temp_to_print;
    for(u_int32_t i = 0; i < table->num_rows; i++)
    {
        int page_index = i / max_rows_in_page;
        //page not present in cache, I have to bring it to memory and then assign it
        if (table->pager.cache_of_pages[page_index] == NULL)
        {
            void* page_buffer = calloc(1, page_size);
            bring_page_into_memory(page_index, table, page_buffer);
            table->pager.cache_of_pages[page_index] = page_buffer;
        }
        int row_offset = i % max_rows_in_page;
        uint32_t byte_offset = row_offset * row_size;
        void* source_pointer = byte_offset + (char *)table->pager.cache_of_pages[page_index];

        copy_row_data_from_table_to_temp_to_print(&temp_to_print, source_pointer);

        print_row(&temp_to_print);
    }
    return EXECUTE_SUCCESS;
}


//////////////////////////////////////////////////////////////////////// execution ////////////////////////////////////////////


MetaCommandResult do_command(InputBuffer* input_buffer, Table* table)
{
    if (strcmp(input_buffer->buffer, ".exit") == 0)
    {
        int num_full_pages = table->num_rows / max_rows_in_page;
        for(int i = 0; i < num_full_pages; i++)
        {
            if (table->pager.cache_of_pages[i] != NULL)
            {
                if (update_file(i, table, page_size) == 0)
                exit(EXIT_FAILURE);
                free(table->pager.cache_of_pages[i]);
                table->pager.cache_of_pages[i] = NULL;
            }
        }
        int additional_row = table->num_rows % max_rows_in_page;
        if (additional_row > 0)
        {
            int page_number = num_full_pages;
            if (table->pager.cache_of_pages[page_number] != NULL)
            {
                size_t size_to_write = additional_row * row_size;
                if (update_file(page_number, table, size_to_write) == 0)
                exit(EXIT_FAILURE);
                free(table->pager.cache_of_pages[page_number]);
                table->pager.cache_of_pages[page_number] = NULL;
            }
        }
        if (table->pager.file_descriptor != NULL) 
        {
            if (fclose(table->pager.file_descriptor) != 0)
            perror("Error closing database file");
            table->pager.file_descriptor = NULL; 
        }
        if (table->pager.filename != NULL) 
        {
            free(table->pager.filename);
            table->pager.filename = NULL;
        }
        free(table);
        close_input_buffer(input_buffer);
        exit(EXIT_SUCCESS);
    }
    else
    {
        return META_COMMAND_UNRECOGNIZED_COMMAND;
    }
}

PreparedStatementResult prepare_statement(InputBuffer* input_buffer, Statement* stmt)
{
    if (strcmp(input_buffer->buffer, "select") == 0)
    {
        stmt->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }
    else if (strncmp(input_buffer->buffer, "insert", 6) == 0 && (input_buffer->buffer[6] == ' '))
    {
        //first word is always insert
        int args_count = 1;
        char* token = strtok(input_buffer->buffer, " ");
        //step past "insert" to get the ID
        token = strtok(NULL, " ");
        while (token != NULL)
        {
            args_count++;
            if (args_count == 2)
            {
                int id = atoi(token);
                if (id < 0)
                {
                    return PREPARE_NEGATIVE_ID;
                }
                else
                {
                    stmt->Row_data_when_insert.id = id;
                }
            }
            else if (args_count == 3)
            {
                if (strlen(token) > max_username_length)
                {
                    return PREPARE_STRING_TOO_LONG;
                }
                else
                {
                    strcpy(stmt->Row_data_when_insert.username, token);
                }
            }
            else if (args_count == 4)
            {
                if (strlen(token) > max_email_length)
                {
                    return PREPARE_STRING_TOO_LONG;
                }
                else
                {
                    strcpy(stmt->Row_data_when_insert.email, token);
                }
            }
            else
            {
                return PREPARE_TOO_MANY_ARGUMENTS;
            }
            token = strtok(NULL, " ");
        }
        if (args_count < 4) 
        {
            return PREPARE_TOO_FEW_ARGUMENTS; 
        }
        stmt->type = STATEMENT_INSERT;
        return PREPARE_SUCCESS;
    }
    else
    return PREPARE_UNRECOGNIZED_STATEMENT;
}

ExecuteResult execute_statement(Statement* stmt, Table* table)
{
    if (stmt->type == STATEMENT_SELECT)
    {
        return execute_select(stmt, table);
    }
    else if (stmt->type == STATEMENT_INSERT)
    {
        return execute_insert(stmt, table);
    }

}


/////////////////////////////////////////////////////////// Main ///////////////////////////////////////////////////////////////

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        printf("Please provide the file name");
        return 0;
    }
    Table* table = table_initializer(argv[1]);
    InputBuffer* input_buffer = input_buffer_initializer();
    while (true)
    {
        print_prompt();
        getInput(input_buffer);
        if (input_buffer->buffer[0] == '.')
        {
            MetaCommandResult command_res = do_command(input_buffer, table);
            if (command_res == META_COMMAND_SUCCESS)
            continue;
            else if (command_res == META_COMMAND_UNRECOGNIZED_COMMAND)
            {
                printf("Unrecognized command '%s'\n", input_buffer->buffer);
                continue;
            }
        }
        Statement stmt;
        PreparedStatementResult prepare_res = prepare_statement(input_buffer, &stmt);
        if (prepare_res == PREPARE_SUCCESS)
        {
            ExecuteResult exec_result = execute_statement(&stmt, table);
            if (exec_result == EXECUTE_SUCCESS)
            {
                printf("Executed.\n");
            }
            else if (exec_result == EXECUTE_TABLE_FULL)
            {
                printf("Couldnt Execute, Table is full\n");
            }
        }
        else if (prepare_res == PREPARE_UNRECOGNIZED_STATEMENT)
        {
            printf("Unrecognized keyword at start of '%s'.\n", input_buffer->buffer);
        }
        else if (prepare_res == PREPARE_SYNTAX_ERROR)
        {
            printf("Syntax error. Could not parse statement.\n");
        }
        else if (prepare_res == PREPARE_STRING_TOO_LONG)
        {
            printf("String is too long.\n");
        }
        else if (prepare_res == PREPARE_TOO_MANY_ARGUMENTS)
        {
            printf("Too many arguments.\n");
        }
        else if (prepare_res == PREPARE_TOO_FEW_ARGUMENTS)
        {
            printf("Too few arguments.\n");
        }
        else if (prepare_res == PREPARE_NEGATIVE_ID)
        {
            printf("ID must be positive.\n");
        }
    }   
}
