'''
	File: perceptron.py
	Author: Grace Willhoite
	Description: The main function perpetron(threshold, adj_factor, init_weights, examples, num_passes) runs a
		weight adjustment algorithm for a certain number of passes on an input to train the perceptron to predict
		a given state.
'''

def perceptron(threshold, adj_factor, init_weights, examples, num_passes):
	'''
		Function: perceptron(threshold, adj_factor, init_weights, examples, num_passes)
		Description: perceptron() is the main function described in the file header and has no return value.
		
		perceptron() has 5 arguments. threshold is a float that if the prediction is less than, makes the prediction False or if greater 
		than True. adj_factor is a float used to adjust the weights given. init_weights is a list of initial weights for 
		prediction calculation. examples is a list of lists of length 2 with the first element a value of True or False for the following 
		given input of 0's and 1's in the second element (e.g. [[True, [0, 1, 1, 0]], [False, [0, 0, 1, 0]]]). The length of the second element must 
		be the same length as the init_weights argument. num_passes is the number of passes desired to train the perceptron with.
	'''

	# formatted print for beginning of perceptron
	print("Starting weights: ", init_weights)
	print("Threshold: " + str(threshold) + "\tAdjustment: " + str(adj_factor))

	cur_weights = init_weights
	for i in range(0, num_passes):
		# for each example, do a pass
		print("\nPass " + str(i) + "\n")

		for example in examples:
			example_pass(example, cur_weights, adj_factor, threshold)
	return

def example_pass(example, weights, adj_factor, threshold):
	'''
	Function: example_pass(example, weights, adj_factor, threshold)
	Description: example_pass() executes a perceptron pass on one example and has no return value. It calculates the prediction value based on an array
	of weights and the example input and adjusts the weight array if the prediction is incorrect by: 1) adding the adjustment factor
	for every 1 in the input if the prediction is False, or 2) subtracting the adjustment factor if the prediction is True.
	There are 4 arguments. example is a list of length 2 with the first element a value of True or False for the following 
	given input of 0's and 1's in the second element (e.g. True, [0, 1, 1, 0]]). weights is a list of weights for the prediction calculation.
	adj_factor is the adjustment factor for adjusting the weight array. threshold is the threshold for the predictions True/False determination.
	'''
	correct_value = example[0]
	example_input = example[1]

	# calculate the prediction value based on the weights and inputs
	prediction_sum = 0
	for j in range(0, len(weights)):
		prediction_sum += weights[j] * example_input[j]

	# determine if output is True/False given the threshold and prediction sum
	if prediction_sum > threshold:
		prediction = True
	else:
		prediction = False

	if prediction != correct_value:
		# adjust weights by adding adj factor to elements where there is a one
		if prediction == True:
			# subtract where there are 1s in the input
			for i in range(0, len(example_input)):
				if example_input[i] == 1:
					weights[i] = weights[i] - adj_factor
		else:
			# add where there are 1s in the input
			for i in range(0, len(example_input)):
				if example_input[i] == 1:
					weights[i] = weights[i] + adj_factor
	# print the formatted result
	printResult(example_input, correct_value, prediction, weights)

	return

def printResult(example_input, correct_value, prediction, weights):
	'''
	Function: printResult(example_input, correct_value, prediction, weights)
	Description: printResult() formats the inputs and displays them to the screen and has no return value
	'''
	print("inputs: ", example_input)
	print("prediction:  " + str(prediction) + "\tanswer:  " + str(correct_value))
	print("adjusted weights:  ", weights )

