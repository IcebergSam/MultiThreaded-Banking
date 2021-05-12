/*********************************
 * Sam Ahsan
 * 29 Nov 2019
 * CS3305 asn3
 *********************************/

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<pthread.h>

// initialise a mutex lock globally we we can call it from our threads
pthread_mutex_t mutex;

//Account structure to hold and access information for each account
//(accounts will be stored in an Account array)
typedef struct {
	int bal;
	char type;

	int d_fee;
	int w_fee;
	int t_fee;

	int tr_limit; // transaction limit before additional fee is applied
	int tr_addFee; // fee to be added if transacts > tr_limit
	int transacts; // transacion counter

	char overdraft;	// set to 'Y' if account has overdraft protection and 'N' otherwise
	int overdraft_applied; // determines if the overdraft fee should be applied
	int overdraft_amount; // set to -1 if overdraft=-'N'
	int OVERDRAFT_LIMIT; // -5000
} Account;

// getRange function determines how many times the overdraft fee shuold be applied
// (without accounting for how many times the fee has already been applied to the account)
int getRange(int val)
{
	if(val >= 0) return 0;
	
	int x, y, count;
	for(x = 0, y = -500,count = 1; y >= -5000; x -= 500, y -= 500, count++)
	{
		if(val < x && val >= y)
			return count;
	}

	return -9999;
}

// structure to send information from the main function to created threads: access
// account array and the string defining client actions as per input file
typedef struct{
	char *client_actions;
	Account *acArray;
} InfoForThreads;   

// transaction function called from each thread
void *transactions(void *acInfo)
{
	// dereference info from parent thread
	InfoForThreads *info = (InfoForThreads *)acInfo;
	char *client = info->client_actions;
	Account *acc = info->acArray;
	int client_num;

	// split string by whitespace	
	const char *delim = " ";	
	char *tok = strtok(client, delim);
	
	while(tok != NULL)	// parse line
	{
		// one case to determine the client number;
		// three cases for 3 possible respective client actions
		// (ie. deposit, withdraw or transfer)
		switch (*tok)
		{
			// determine client number
			case 'c' : 
			{	
				if(strlen(tok) > 2)
					client_num = (((int)tok[1] - '0') * 10) + (int)(tok[2] - '0');
				else
					client_num = tok[1] - '0';
				break;
			}

			// make a deposit
			case 'd' : 
			{	
				char type = *tok;
				
				// determine which account is receiving the deposit
				tok = strtok(NULL, delim);
				int acc_num;
				if(strlen(tok) > 2)
					acc_num = (((int)tok[1] - '0') * 10) + (int)(tok[2] - '0');
				else
					acc_num = tok[1] - '0';
				
				// determine amount to be deposited
				tok = strtok(NULL, delim);
				int amount = atoi(tok);
				
				// lock the accounts array 
				pthread_mutex_lock(&mutex);
				
				// update account information
				acc[acc_num - 1].bal += amount;
				acc[acc_num - 1].bal -= acc[acc_num - 1].d_fee;
				acc[acc_num - 1].transacts += 1;
				
					// check and update if additional fee applies
				if(acc[acc_num - 1].transacts > acc[acc_num - 1].tr_limit)
					acc[acc_num - 1].bal -= acc[acc_num - 1].tr_addFee;
				
				// overdraft fee can only be applied if the current transaction pushes the balance
				// to the next overdraft range further from zero. This does not apply for deposits since the 
				// balance can only increase. If the balance is increased such that it passes a range boundary
				// towards zero, then the overdraft_applied calue in the account should be decreased
				int update_OD = getRange(acc[acc_num - 1].bal);
				if(update_OD <= 0)
					acc[acc_num - 1].overdraft_applied = 0;
				else
					acc[acc_num - 1].overdraft_applied = update_OD;
				
				// unlock the accounts array 
				pthread_mutex_unlock(&mutex);

				/********* TESTING IF DEPOSITIS APPLIED SUCCESSFULLY **********/
				/* printf("Client %d deposits %d into account %d\n", client_num, amount, acc_num);
				 * printf("\ttesting: Acc%d bal = %d\n", acc_num, acc[acc_num - 1].bal);		*/
				break;
			}

			//make a withdrawal
			case 'w' :
			{	char type = *tok;
				tok = strtok(NULL, delim);
				
				// determine which account the withdrawal comes from
				int acc_num;
				if(strlen(tok) > 2)
					acc_num = (((int)tok[1] - '0') * 10) + (int)(tok[2] - '0');
				else
					acc_num = tok[1] - '0';
				
				// determine the amount to be withdrawn
				tok = strtok(NULL, delim);
				int amount = atoi(tok);

				// lock the accounts array 
				pthread_mutex_lock(&mutex);
				
				// before doing the withdrawal calculate to make sure the account has sufficient funds
				int newbal = acc[acc_num - 1].bal - amount;
				newbal -= acc[acc_num - 1].w_fee;

				//add fee if transaction number > limit
				if(acc[acc_num - 1].transacts +1 > acc[acc_num - 1].tr_limit)
					newbal -= acc[acc_num - 1].tr_addFee;

				// calculate the OD fee according to which overdraft range the account balance would
				// fall into if the withrawal were made. if the result of this calculation is greater
				// the number of times the od has already been applied then multiply the difference
				// by the overdraft fee and subtract the result from the calculated new balance. if
				// the new balance pushes the account balance into a range which required another
				// overdraft fee to be applied, then subtract another overdraft fee and update values. 
				if(newbal < 0)
				{
					int required_OD = getRange(newbal);
					if(required_OD > acc[acc_num - 1].overdraft_applied)
						newbal -= ((required_OD - acc[acc_num - 1].overdraft_applied) *
								 acc[acc_num - 1].overdraft_amount);
				}
				
				// if the account has sufficient funds to allow the withdrawal then update account info
				if(newbal >= 0 || (acc[acc_num - 1].overdraft == 'Y' && 
							newbal >= acc[acc_num - 1].OVERDRAFT_LIMIT))
				{
					acc[acc_num - 1].bal -= amount;
					acc[acc_num - 1].bal -= acc[acc_num - 1].w_fee;
					
					acc[acc_num - 1].transacts =+ 1;
				
					if(acc[acc_num - 1].transacts > acc[acc_num - 1].tr_limit)
						acc[acc_num - 1].bal -= acc[acc_num - 1].tr_addFee;
					
					if(acc[acc_num - 1].bal < 0)
					{
						// same calculation for OD fee here as the calculation before applying the fee
						// this time the account info is updated according to the result instead of only
						// using the result to compare values.
						int apply_OD = getRange(acc[acc_num - 1].bal);
						if(apply_OD > acc[acc_num - 1].overdraft_applied)
						{
							acc[acc_num - 1].bal -= ((apply_OD - acc[acc_num - 1].overdraft_applied) *
								       	acc[acc_num - 1].overdraft_amount);
							
							acc[acc_num - 1].overdraft_applied = apply_OD;
							
							// subtract and update another OD fee if the balance falls into a lower range
							if(getRange(acc[acc_num - 1].bal) > apply_OD)
							{
								acc[acc_num - 1].bal -= acc[acc_num - 1].overdraft_amount;
								acc[acc_num - 1].overdraft_applied++;
							}
						}
					}
				}

				// unlock the accounts array 
				pthread_mutex_unlock(&mutex);

				/********* TESTING IF WITHDRAW APPLIED SUCCESSFULLY *************/
				/* printf("Client %d withdraws %d from account %d\n", client_num, amount, acc_num);
				 * printf("Acc%d bal = %d\n", acc_num, acc[acc_num - 1].bal);			*/

				break;
			}
			case 't' :
			{	
				char type = *tok;
				
				tok = strtok(NULL, delim);
				
				// determine the account sending the transfer
				int acc_from;
				if(strlen(tok) > 2)
					acc_from = (((int)tok[1] - '0') * 10) + (int)(tok[2] - '0');
				else
					acc_from = tok[1] - '0';
				
				tok = strtok(NULL, delim);
				
				// determine the account receiving the transfer
				int acc_to;
				if(strlen(tok) > 2)
					acc_to = (((int)tok[1] - '0') * 10) + (int)(tok[2] - '0');
				else
					acc_to = tok[1] - '0';
				
				tok = strtok(NULL, delim);
				
				// the following calculations are exactly the same and described by the calculations
				// made during a withdrawal or transfer. first it is determined if the sender has sufficient
				// funds for the transaction; if yes then a withdrawal is applied to the sender's account and
				// a deposit is applied to the receiver's account and all values are updates for each account
				int amount = atoi(tok);
				
				// lock the accounts array 
				pthread_mutex_lock(&mutex);
				
				int acc1_newbal = acc[acc_from].bal - amount;
				acc1_newbal -= acc[acc_from].t_fee;
				
				if(acc[acc_from].transacts +1 > acc[acc_from].tr_limit)
					acc1_newbal -= acc[acc_from].tr_addFee;
				
				if(acc1_newbal < 0)
				{
					int required_OD = getRange(acc1_newbal);
						if(required_OD > acc[acc_from - 1].overdraft_applied)
							acc1_newbal -= ((required_OD - acc[acc_from - 1].overdraft_applied) *
									acc[acc_from - 1].overdraft_amount);
				}
				
				if(acc1_newbal >= 0 || (acc[acc_from - 1].overdraft == 'Y' && 
							acc1_newbal >= acc[acc_from - 1].OVERDRAFT_LIMIT))
				{
					acc[acc_from - 1].bal -= amount;
					acc[acc_from - 1].bal -= acc[acc_from - 1].t_fee;
					acc[acc_from - 1].transacts += 1;
					
					if(acc[acc_from - 1].transacts > acc[acc_from - 1].tr_limit)
						acc[acc_from - 1].bal -= acc[acc_from - 1].tr_addFee;
					
					if(acc[acc_from - 1].bal < 0)
					{
						int apply_OD = getRange(acc[acc_from - 1].bal);
						
						acc[acc_from - 1].bal -= ((apply_OD - acc[acc_from - 1].overdraft_applied) *
								acc[acc_from - 1].overdraft_amount);
						
						acc[acc_from - 1].overdraft_applied = apply_OD;
						
						if(getRange(acc[acc_from - 1].bal) > apply_OD)
						{
							acc[acc_from - 1].bal -= acc[acc_from - 1].overdraft_amount;
							acc[acc_from - 1].overdraft_applied++;
						}
					}
					
					acc[acc_to - 1].bal += amount;
					acc[acc_to - 1].bal -= acc[acc_to - 1].t_fee;
					acc[acc_to - 1].transacts += 1;
					
					if(acc[acc_to - 1].transacts > acc[acc_to - 1].tr_limit)
                                                       acc[acc_to - 1].bal -= acc[acc_to - 1].tr_addFee;
						int update_OD = getRange(acc[acc_to - 1].bal);
					
					if(update_OD <= 0)
						acc[acc_to - 1].overdraft_applied = 0;
					else
						acc[acc_to - 1].overdraft_applied = update_OD;
				
					// unlock the accounts array 
					pthread_mutex_unlock(&mutex);

					/********* TESTING IF TRANSFER APPLIED SUCCESSFULY  *********************/
					/* printf("Client %d transfers %d from account %d to account %d\n", client_num, amount, acc_from, acc_to);
					 * printf("Acc%d bal= %d\tAcc%d bal= %d\n\n", acc_from, acc[acc_from - 1].bal, acc_to, acc[acc_to - 1].bal); */
			
					break;
				}
			}
		}

		tok = strtok(NULL, delim);
	}
}

int main()
{

	FILE *inf = fopen("assignment_3_input_file.txt", "r");
	
	if(inf == NULL)
	{
		printf("error: Input file not opened\n");
		exit(1);
	}
	
	FILE *outf = fopen("assignment_3_output_file.txt", "w");	

	if(outf == NULL)
	{
		printf("error: Output file not created\n");
		exit(1);
	}

	// Parse the input file.
	// Copy each line beginning with 'a' in a string array accts[]
	// Copy each line beginning with 'c' in a string array clients[]
	// Copy each line beginning with 'd' in a string array deps[]
	// Allows for up to 100 accounts, clients, and depositors,
	// each of which may have actions defined in 100 chars or less
	// ie. assume that each line of the input file is <= 100 chars
	char accts[100][100]; 
	int num_accts = 0;
	
	char clients[100][100];
	int num_clients = 0;
	
	char deps[100][100];
	int num_deps = 0;
	
	char c;
	while((c = fgetc(inf)) != EOF)
	{
		if (c == 'a')
		{
			int i;
			for(i = 0; c != '\n'; i++)
			{
				accts[num_accts][i] = c;
				c = fgetc(inf);
			}

			num_accts++;
			continue;
		}
		
		if (c == 'c')
		{
			int i;
			for(i = 0; c != '\n'; i++)
			{
                                clients[num_clients][i] = c;
				c = fgetc(inf);
			}
			
			num_clients++;
			continue;
		}
		
		if (c == 'd')
		{
			int i;
			for(i = 0; c != '\n'; i++)
			{
                                deps[num_deps][i] = c;
				c = fgetc(inf);
			}
			
			num_deps++;
		}
	}

	// For each string in accts[], create a struct Account
	// Parse the string and use information to fill created Account
	// Initial values for account bal, num transactions,overdraft set to 0
	// Then add account to a struct Account array accounts[]
	Account accounts[num_accts];
	int i;
	for(i=0; i < num_accts; i++)
	{
		// create account and set initial values
		Account acc;
		acc.bal = 0;
		acc.transacts = 0;
		acc.overdraft_applied = 0;
		acc.OVERDRAFT_LIMIT = -5000;
		
		// split string at whitespace		
		const char *delim = " ";
		char *tok = strtok(accts[i], delim);
		
		while(tok != NULL)
		{		
			switch(*tok){
				case 'b':
					acc.type = 'b';
					break;

				case 'p' :
					acc.type = 'p';
					break;

				case 'd' :
					tok = strtok(NULL, delim); 
					acc.d_fee = atoi(tok);
					break;
			
				case 'w' :
					tok = strtok(NULL, delim);
					acc.w_fee = atoi(tok);
					break;
			
				case 't' :
					if(strlen(tok) == 4){ 
						break; 
					}
					if(strlen(tok) == 1){
						tok = strtok(NULL, delim);
						acc.t_fee = atoi(tok); 
						break;
					}
					if(strlen(tok) == 12){
						tok = strtok(NULL, delim);
						acc.tr_limit = atoi(tok);
						
						tok = strtok(NULL, delim);
						acc.tr_addFee = atoi(tok);
						break;
					}
				case 'o' :
					tok = strtok(NULL, delim);
					acc.overdraft = *tok;
					
					if(*tok == 'Y')
					{
						tok = strtok(NULL, delim);
						acc.overdraft_amount = atoi(tok);
						break;
					}
					if(*tok == 'N')
					{ 
						acc.overdraft_amount = -1; 
						break;  
					}
			}

			tok = strtok(NULL, delim);
			accounts[i] = acc;
		}

	/********* TESTING IF ACCOUNTS INITIALISED CORRECTLY **********/
	/* printf("Acc%d\ntype= %c\nd_fee= %d\nw_fee= %d\nt_fee= %d\ntr_limit= %d\nt_addFee= %d\noverdraft_amount= %d\n", 
	 *		i+1, acc.type, acc.d_fee, acc.w_fee, acc.t_fee, acc.tr_limit, acc.tr_addFee, acc.overdraft_amount);   */
	
	}

	// Parse each string in the deps[] array to apply initial deposits to accounts, then update account info
	for(i=0; i < num_deps; i++)
	{
		const char *delim = " ";
		char *tok = strtok(deps[i], delim); // split string by whitespace
		
		while(tok != NULL)
		{
			if(*tok == 'a')
			{
				char accnum = (char)tok[1]; 
				const char *n = &accnum;
				int acc_num = atoi(n);
				
				tok = strtok(NULL, delim);
				int amount = atoi(tok);
				
				//update account info to make deposit
				accounts[acc_num - 1].bal += amount;
				accounts[acc_num - 1].bal -= accounts[acc_num - 1].d_fee;
				accounts[acc_num - 1].transacts += 1;
			}
			
			tok = strtok(NULL, delim);
		}
	}


	/********* TESTING IF INITIAL DEPOSITIS WERE APPLIED CORRECTLY **********/
	/* for(i=0; i<num_accts; i++)							
	 * 	printf("After initial deposits: a%d bal= %d\n", i+1, accounts[i].bal); */


	// Create a thread  for each client in the clients array
	// Apply mutual exclusion when a cliient is reading/writing data
	// Client transactions determined by parsing strings in clients array
	// (i.e. each line from input file beginning with 'c')
	for(i=0; i < num_clients; i++)
	{
		// create structure to send various data to thread. accounts array 
		// is the shared memory which must have a mutex lock applied when
		// a thread is accessing it. 
		InfoForThreads info;
		info.client_actions = clients[i];
		info.acArray = accounts;	
		
		pthread_t run_transactions;
		if(pthread_create(&run_transactions, NULL, transactions, &info))
		{
			printf("thread error\n");
			fprintf(outf, "thread error\n");
			exit(1);
		}
	}

	// print updated values of all accounts to stdout and to output file
	for(i=0; i<num_accts; i++)
	{
		char *type;
		if(accounts[i].type == 'p')
			type = "personal";
		if(accounts[i].type == 'b')
			type = "business";

		printf("a%d type %s %d\n", i+1, type, accounts[i].bal);
		fprintf(outf, "a%d type %s %d\n", i+1, type, accounts[i].bal);
	}

	fclose(inf);
	fclose(outf);
	
	exit(0); 
	// phew
}
