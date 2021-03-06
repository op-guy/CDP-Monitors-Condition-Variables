#include <bits/stdc++.h>
#include <fstream>
#include <pthread.h>
#include <unistd.h>
using namespace std;
// Actual Database with name mapped to value, eg. 'u' : 100
std::unordered_map<std::string, int> vars;

// Function to split a line from text file
std::vector<std::string> splitWord(std::string str)
{
	std::vector<std::string> res;
	std::string word = "";
	for (auto x : str)
	{
		if (x == ' ')
		{
			res.push_back(word);
			word = "";
		}
		else
		{
			word = word + x;
		}
	}
	// For eg. if str = u = u + v then res = [u,=,u,+,v]
	res.push_back(word);
	return res;
}
// Custom data struct to store operation details to be done on databse variable (u = u + 100 or u = u + v)
struct Operation
{
	std::string varname; //databaes variable
	std::string op;		 //operation char eg. R/W/C

	bool isOtherVar;	  //bool to tell if another variable involved in operation
	std::string otherVar; //to store that other var

	bool isVal; // bool to check if there is an int value involved in operation
	int value;	// if yes then to store that value

	// final assignment with passed arguments
	Operation(std::string s1, std::string c1, bool f1, bool f2, std::string s2, int v1)
	{
		varname = s1;
		op = c1;
		isOtherVar = f1;
		isVal = f2;
		otherVar = s2;
		value = v1;
	}
};

// Data struture to store lock request (R u or W u)
struct Request
{
	std::string type;	 //type of request
	std::string varname; //databse variable associated with request

	Request(std::string t, std::string v)
	{
		type = t;
		varname = v;
	}
};

// Transaction class to extract data for transaction
class Transaction
{
private:
	int tid;
	std::vector<Operation> opSeq; //vector to store all operations of a transaction
	std::vector<Request> reqSeq;  // vector to store lock requests
	std::string seq;			  //sequence string to store the sequence of what to be done ORORORRO
public:
	Transaction() {}

	Transaction(int id)
	{
		tid = id;
		seq = "";
	}

	void addOperation(Operation O)
	{
		seq.push_back('O'); //O means perform operation
		opSeq.push_back(O);
	}

	void addRequest(Request R)
	{
		seq.push_back('R'); //R means lock request
		reqSeq.push_back(R);
	}
	std::vector<Operation> getopSeq()
	{
		return opSeq;
	}
	std::vector<Request> getreqSeq()
	{
		return reqSeq;
	}
	std::string getseq()
	{
		return seq;
	}

	// Function to return Tid for a particular transaction
	int getId()
	{
		return tid;
	}
};

// Lockmngr class for locking functions
class LockMgr
{
private:
	pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER; //single lock for all

	std::unordered_map<std::string, pthread_cond_t> condvar; //condvar for each database var
	std::unordered_map<std::string, int> active_read;		 //active readers on each database var
	std::unordered_map<std::string, int> active_write;		 // active writers on each database var
	std::unordered_map<std::string, int> waiting_read;		 // waiting readers on each database var
	std::unordered_map<std::string, int> waiting_write;		 // waiting witers on each database var
	std::unordered_map<std::string, std::queue<Transaction>> Q;
	//mp keeps track of what lock is held by what tid
	std::unordered_map<int, unordered_map<string, int>> mp; // 0  for not present, 1 for read-only, 2 for write-read

public:
	// Initialize everything
	LockMgr()
	{
		lock = PTHREAD_MUTEX_INITIALIZER;
		for (auto itr = vars.begin(); itr != vars.end(); itr++)
		{
			condvar[itr->first] = PTHREAD_COND_INITIALIZER;
		}
	}
	// for R
	bool acquireReadLock(int tid, std::string varname)
	{
		pthread_mutex_lock(&lock);

		while (active_write[varname] > 0)
		{
			waiting_read[varname]++;
			pthread_cond_wait(&condvar[varname], &lock);
			waiting_read[varname]--;
		}

		mp[tid][varname] = 1;
		active_read[varname]++;
		pthread_mutex_unlock(&lock);

		return true;
	}
	// for W
	bool acquireWriteLock(int tid, std::string varname)
	{
		pthread_mutex_lock(&lock);

		while (active_write[varname] > 0 || active_read[varname] > 0)
		{
			waiting_write[varname]++;
			pthread_cond_wait(&condvar[varname], &lock);
			waiting_write[varname]--;
		}

		mp[tid][varname] = 2;
		active_write[varname]++;
		pthread_mutex_unlock(&lock);

		return true;
	}
	// for R -> W
	bool upgradeToWrite(int tid, std::string varname)
	{
		if (mp[tid][varname] != 1)
			return false;

		pthread_mutex_lock(&lock);

		while (active_write[varname] > 1 || active_read[varname] > 0)
		{
			waiting_write[varname]++;
			pthread_cond_wait(&condvar[varname], &lock);
			waiting_write[varname]--;
		}

		mp[tid][varname] = 2;
		active_read[varname]--;
		active_write[varname]++;
		pthread_mutex_unlock(&lock);

		return true;
	}
	// to release lock
	bool releaseLock(int tid, std::string varname)
	{
		pthread_mutex_lock(&lock);

		if (mp[tid][varname] == 2)
		{
			mp[tid][varname] = 0;
			active_write[varname]--;
			pthread_cond_broadcast(&condvar[varname]);
		}
		else if (mp[tid][varname] == 1)
		{
			mp[tid][varname] = 0;
			active_read[varname]--;
			if (active_read[varname] == 0 && waiting_write[varname] > 0)
			{
				pthread_cond_broadcast(&condvar[varname]);
			}
		}

		pthread_mutex_unlock(&lock);

		return true;
	}
};
// Function to check if string contains a digit
bool isNumber(std::string s)
{
	for (int i = 0; i < s.size(); i++)
		if (isdigit(s[i]) == false)
			return false;

	return true;
}
// Execution function for each transaction
void *runTransaction(void *T)
{
	Transaction *trx;
	// trx is current transaction instance
	trx = (Transaction *)T;
	// Get all data related to this transaction
	std::vector<Operation> opseq = trx->getopSeq();
	std::vector<Request> reqseq = trx->getreqSeq();
	string seq = trx->getseq();
	unordered_map<std::string, int> map1; //This map is local copy of database, finally this will written to global
	unordered_map<std::string, int> map2; //This map stores value after performing operation on var
	// LockMngr Object
	LockMgr locker;

	for (int i = 0; i < seq.size(); i++)
	{
		// If there is request in seq
		if (seq[i] == 'R')
		{
			// if it is a read request
			if (reqseq[i].type == "R")
			{
				locker.acquireReadLock(trx->getId(), reqseq[i].varname);
				map1[reqseq[i].varname] = vars[reqseq[i].varname];
			}
			// If it is a W request
			else
			{
				if (locker.upgradeToWrite(trx->getId(), reqseq[i].varname))
				{
					map1[reqseq[i].varname] = map2[reqseq[i].varname];
				}
				else
				{
					locker.acquireWriteLock(trx->getId(), reqseq[i].varname);
					map1[reqseq[i].varname] = map2[reqseq[i].varname];
				}
			}
		}
		// If there is an Operation in seq
		else
		{
			// Check if already have write lock
			if (locker.upgradeToWrite(trx->getId(), opseq[i].varname))
			{
				// Check if there is other variable present
				if (opseq[i].isOtherVar)
				{
					if (opseq[i].op == "+")
					{
						map2[opseq[i].varname] = map1[opseq[i].varname] + map1[opseq[i].otherVar];
					}
					else
					{
						map2[opseq[i].varname] = map1[opseq[i].varname] - map1[opseq[i].otherVar];
					}
				}
				else
				{
					if (opseq[i].op == "+")
					{
						map2[opseq[i].varname] = map1[opseq[i].varname] + opseq[i].value;
					}
					else
					{
						map2[opseq[i].varname] = map1[opseq[i].varname] - opseq[i].value;
					}
				}
			}
			// If not then acquire write lock
			else
			{
				locker.acquireWriteLock(trx->getId(), opseq[i].varname);
				if (opseq[i].isOtherVar)
				{
					if (opseq[i].op == "+")
					{
						map2[opseq[i].varname] = map1[opseq[i].varname] + map1[opseq[i].otherVar];
					}
					else
					{
						map2[opseq[i].varname] = map1[opseq[i].varname] - map1[opseq[i].otherVar];
					}
				}
				else
				{
					if (opseq[i].op == "+")
					{
						map2[opseq[i].varname] = map1[opseq[i].varname] + opseq[i].value;
					}
					else
					{
						map2[opseq[i].varname] = map1[opseq[i].varname] - opseq[i].value;
					}
				}
			}
		}
	}
	// After completion of trx i.e C, write map1 to global
	for (auto i : map1)
	{
		vars[i.first] = i.second;
	}
	for (int i = 0; i < seq.size(); i++)
	{
		if (seq[i] == 'R')
		{
			if (reqseq[i].type == "R")
			{
				locker.releaseLock(trx->getId(), reqseq[i].varname);
			}
			else
			{
				locker.releaseLock(trx->getId(), reqseq[i].varname);
			}
		}
		else
		{
			locker.releaseLock(trx->getId(), opseq[i].varname);
		}
	}

	return NULL;
}

// LockMgr *locker;

int main()
{
	std::string text;
	std::ifstream file("input.txt");

	getline(file, text); // number of transactions N
	std::vector<std::string> temp = splitWord(text);

	int N = stoi(temp[0]);

	getline(file, text); // variables and their values
	temp = splitWord(text);
	// Put the initial values
	for (int i = 0; i < temp.size(); i++)
	{
		if (i % 2)
		{
			vars[temp[i - 1]] = stoi(temp[i]);
		}
		else
		{
			vars[temp[i]] = 0;
		}
	}
	cout << "initial Data: "
		 << "\n\n";
	for (auto i : vars)
	{
		cout << i.first << " = " << i.second << "\n";
	}

	// array for each transaction
	Transaction tarr[N];

	int counter = 0;
	// fill data in that array
	while (counter < N)
	{
		getline(file, text);
		int tid = stoi(text);

		Transaction T(tid);
		while (text != "" && text != "C")
		{
			getline(file, text);

			if (text == "C")
				break;

			std::vector<std::string> vec = splitWord(text);

			if (vec.size() == 2) // This means that line is smthing like "R u"
			{
				Request R(vec[0], vec[1]);
				T.addRequest(R);
			}
			// Else it is a math operation like u = u + v
			else
			{
				// check if last thing is a number or var
				bool f = isNumber(vec.back());
				int num;

				if (f)
					num = std::stoi(vec[4]);
				else
					num = 0;
				// store the operation
				Operation O(vec[0], vec[3], !f, f, vec[4], num);
				T.addOperation(O);
			}
		}
		// Store all the info for this transaction in array and move to nxt
		tarr[counter++] = T;
	}

	// Now execute every transction
	pthread_t threads[N];
	for (int i = 0; i < N; i++)
	{
		pthread_create(&threads[i], NULL, runTransaction, (void *)&tarr[i]);
	}
	for (int i = 0; i < N; i++)
	{
		pthread_join(threads[i], NULL);
	}
	cout << "final Data: "
		 << "\n";
	for (auto i : vars)
	{
		cout << i.first << " = " << i.second << "\n";
	}
	std::cout << "The End"
			  << "\n";
	file.close();
}