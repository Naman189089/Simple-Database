#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>


///////////////////////////////////////////////////////////////// IO //////////////////////////////////////////////////////////////

typedef struct
{
    char* buffer;
    size_t buffer_length;
    ssize_t input_length;
} InputBuffer;

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
    printf("Database >");
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



//////////////////////////////////////////////////////////////////////// execution ////////////////////////////////////////////

typedef enum
{
    META_COMMAND_SUCCESS,
    META_COMMAND_UNRECOGNIZED_COMMAND
} MetaCommandResult;

typedef enum
{
    PREPARE_SUCCESS,
    PREPARE_UNRECOGNIZED_STATEMENT
} PreparedStatementResult;

typedef enum
{
    STATEMENT_INSERT, 
    STATEMENT_SELECT
} StatementType;

typedef struct
{
    StatementType type;
} Statement;

MetaCommandResult do_command(InputBuffer* input_buffer)
{
    if (strcmp(input_buffer->buffer, ".exit") == 0)
    {
        close_input_buffer(input_buffer);
        exit(EXIT_SUCCESS);
    }
    else if (strcmp(input_buffer->buffer, ".skibidi") == 0)
    {
        return META_COMMAND_SUCCESS;
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
        stmt->type = STATEMENT_INSERT;
        return PREPARE_SUCCESS;
    }
    else
    return PREPARE_UNRECOGNIZED_STATEMENT;
}

void execute_statment(Statement* stmt)
{
    if (stmt->type == STATEMENT_SELECT)
    {
        printf("This is where we would do a select.\n");
    }
    else if (stmt->type == STATEMENT_INSERT)
    {
        printf("This is where we would do an insert.\n");
    }
}


/////////////////////////////////////////////////////////// Main ///////////////////////////////////////////////////////////////

int main(int argc, char* argv[])
{
    InputBuffer* input_buffer = input_buffer_initializer();
    while (true)
    {
        print_prompt();
        getInput(input_buffer);
        if (input_buffer->buffer[0] == '.')
        {
            if (do_command(input_buffer) == META_COMMAND_SUCCESS)
            continue;
            else if (do_command(input_buffer) == META_COMMAND_UNRECOGNIZED_COMMAND)
            {
                printf("Unrecognized command '%s'\n", input_buffer->buffer);
                continue;
            }
        }
        Statement stmt;
        if (prepare_statement(input_buffer, &stmt) == PREPARE_SUCCESS)
        {
            execute_statment(&stmt);
            printf("Executed\n");
        }
        else if (prepare_statement(input_buffer, &stmt) == PREPARE_UNRECOGNIZED_STATEMENT)
        {
            printf("Unrecognized keyword at start of '%s'.\n", input_buffer->buffer);
        }
    }   
}