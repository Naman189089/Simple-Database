#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

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


typedef struct
{
    int num_rows;
    void* pages[max_pages_in_table];
} Table;

Table* table_initializer()
{
    Table* table = (Table *)malloc(sizeof(Table));
    table->num_rows = 0;
    for(int i = 0; i < max_pages_in_table; i++)
    {
        table->pages[i] = NULL;
    }
    return table;
}

typedef enum
{
    META_COMMAND_SUCCESS,
    META_COMMAND_UNRECOGNIZED_COMMAND
} MetaCommandResult;

typedef enum
{
    PREPARE_SUCCESS,
    PREPARE_UNRECOGNIZED_STATEMENT,
    PREPARE_SYNTAX_ERROR
} PreparedStatementResult;

typedef enum
{
    STATEMENT_INSERT, 
    STATEMENT_SELECT
} StatementType;

typedef struct
{
    uint32_t id;
    char username[max_username_length];
    char email[max_email_length];
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

        //malloc another page in this table
        if (table->pages[page_index] == NULL)
        {
            table->pages[page_index] = malloc(page_size);
        }

        //find out teh row index within the page and the offset to where we need to copy to.
        int row_offset = table->num_rows % max_rows_in_page;
        uint32_t byte_offset = row_offset * row_size;
        void* destination_pointer = byte_offset + (char *)table->pages[page_index];
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
        if (table->pages[page_index] == NULL)
        {
            table->pages[page_index] = malloc(page_size);
        }
        int row_offset = i % max_rows_in_page;
        uint32_t byte_offset = row_offset * row_size;
        void* source_pointer = byte_offset + (char *)table->pages[page_index];

        copy_row_data_from_table_to_temp_to_print(&temp_to_print, source_pointer);

        print_row(&temp_to_print);
    }
    return EXECUTE_SUCCESS;
}




//////////////////////////////////////////////////////////////////////// execution ////////////////////////////////////////////


MetaCommandResult do_command(InputBuffer* input_buffer)
{
    if (strcmp(input_buffer->buffer, ".exit") == 0)
    {
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
        char temp;
        int values = sscanf(input_buffer->buffer, "insert %d %s %s %c", &(stmt->Row_data_when_insert.id), stmt->Row_data_when_insert.username, stmt->Row_data_when_insert.email, &temp);
        if (values != 3)
        {
            return PREPARE_SYNTAX_ERROR;
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
    Table* table = table_initializer();
    InputBuffer* input_buffer = input_buffer_initializer();
    while (true)
    {
        print_prompt();
        getInput(input_buffer);
        if (input_buffer->buffer[0] == '.')
        {
            MetaCommandResult command_res = do_command(input_buffer);
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
                printf("Executed\n");
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
    }   
}
