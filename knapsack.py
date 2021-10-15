weight = 100
w = [0, 100, 50, 45, 20, 10, 5]
v = [0, 40, 35, 18, 4, 10, 2]

# Answer with this input: max_value = 55, items in knapsack are [6, 3, 2]

def knapsack(weight, v, w):
    n = len(w)

    c = [0]*(n)

    for i in range(n):
        c[i] = [0]*(weight+1)

    for i in range(n):

        for j in range(weight+1):
            # initializing row 0 and column 0
            if i == 0 or j == 0:
                c[i][j] = 0

            elif w[i] > j:
                # otherwise, if the weight of our object is heavier than
                # the current running weight (the j index), don't take the
                # item and use the value above
                c[i][j] = c[i-1][j]
            else:
                # now have two choices
                # Choice1: the value of the current object + the value of the
                # previous running total stored in the row above
                
                choice1 = v[i] + c[i-1][j - w[i]]
                # Choice2: don't take the item and use the running value above
                choice2 = c[i-1][j]

                # whichever one is the max, choose that value and put it in the
                # table
                if choice1 > choice2:
                    c[i][j] = choice1
                else:
                    c[i][j] = choice2
                
    return c

# get table
print(*knapsack(weight, v, w), sep="\n")

ans = knapsack(weight, v, w)

#traceback
n = len(v) - 1
my_knap = []

curr_val = ans[n][weight]
curr_weight = weight


for i in range(n, 0, -1):
    # If the weight above is different than the current weight, that object
    # is added to the knapsack and the total value and total weight is changed
    if ans[i][curr_weight] != ans[i-1][curr_weight]:
        curr_val -= v[i]
        curr_weight -= w[i]
        my_knap.append(i)
    else:
        pass

print(my_knap)




