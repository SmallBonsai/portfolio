##DnD_roller.py
##Nov 22, 2018
def gameStart():
    rule = input("What rule set are you using *will be set for whole game* (1 = casual, 2 = Strict): ")
    stop = False
    while stop == False:
        checkType(rule, stop)       
          
def checkType(rule, stop):
    #what check (i.e. combat, skill)
    check_type = input("Is the check combat or skill? (1/2/n to quit): ")
    if check_type == '1':
        combatCheck()
    elif check_type == '2':
        statCheck(rule)
    elif check_type == 'n':
        quit()              

def statCheck(rule):
    from random import randint
     #add to combat not for this ruling but depends on how in depth statchecking is
    d20roll = randint(1,20)
    
    check = criticalCheck(d20roll)
    

    if rule == '1':
        passFailCasual(check)
    else:
        DC = int(input("What is the DC for this skill check? "))
        passFailStrict(DC, check)
           
    return check

def passFailCasual(check):
    fail = False            #not necessary but in case need to know if fail can know
    
    if check >= 2 and check <= 5:
        print("Bad Fail. You will not succeed your attempt, and a negative outcome will arise.")
        fail = True
        return fail
    elif check >= 6 and check <= 10:
        print("Fail. You will not succeed your attempt, but nothing else bad will occur.")
        fail = True
        return fail
    elif check >= 11 and check <= 15:
        print("Succeed!")
        fail = False
        return fail
    elif check >= 16 and check <= 19:
        print("Good Pass. You’ll succeed and something positive will also occur.")
        fail = False
        return fail

def passFailStrict(DC, check):
    fail = False
    if check >= DC:
        print("Succeed")
        fail = False
        return fail
    else:
        print("Fail")
        fail = True
        return fail

def combatCheck():
    
    from random import randint
    DC = int(input("What is the DC for this skill check?")) #add to combat not for this ruling but depends on how in depth statchecking is
    d20roll = randint(1,20)
    
    check = criticalCheck(d20roll)

    result = passFailStrict(DC, check)
    
def criticalCheck(d20roll):
    if d20roll == 1:            ## Check ruling for if modifiers matter for nat rolls
        print("Critical Fail")
        check = 1
        return check
    
    elif d20roll == 20:
        print("Critical Success")
        check = 20
        return check
    
    else:       
        modifier_ask = input("Are there any modifiers? (ex. +2, -2, 0): ")

        add_subtract = modifier_ask[0]
        modifier = modifier_ask[1:len(modifier_ask)]
    
        
        if add_subtract == '+':
            check = d20roll + int(modifier)

        elif add_subtract == '-':
            check = d20roll - int(modifier)

        else:
            check = d20roll
            add_subtract = '+'
            modifier = 0
            
        print(d20roll, add_subtract, modifier, '=', check)
        return check
