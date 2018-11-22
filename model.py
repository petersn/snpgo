#!/usr/bin/python

import numpy as np
import tensorflow as tf

class GoResNet:
	INPUT_FEATURE_COUNT = 22
	FILTERS = 256
	CONV_SIZE = 3
	BLOCK_COUNT = 10
	OUTPUT_SOFTMAX_COUNT = 19 * 19

	def __init__(self, scope_name):
		with tf.variable_scope(scope_name):
			self.build_graph()

	def build_graph(self):
		self.input_ph = tf.placeholder(
			dtype=tf.float32,
			shape=[None, 19, 19, self.INPUT_FEATURE_COUNT],
			name="input_ph",
		)
		self.desired_policy_ph = tf.placeholder(
			dtype=tf.float32,
			shape=[None, 19, 19],
			name="desired_policy_ph",
		)
		self.learning_rate_ph = tf.placeholder(
			dtype=tf.float32,
			shape=[],
			name="learning_rate_ph",
		)
		self.is_training_ph = tf.placeholder(
			dtype=tf.bool,
			shape=[],
			name="is_training_ph",
		)

		self.flow = self.input_ph
		# Stack an initial convolution.
		self.stack_convolution(3, self.INPUT_FEATURE_COUNT, self.FILTERS)
		self.stack_nonlinearity()
		# Stack some number of residual blocks.
		for i in xrange(self.BLOCK_COUNT):
			with tf.variable_scope("block%i" % i):
				self.stack_block()
		# Stack a final batch-unnormalized 1x1 convolution.
		self.stack_convolution(1, self.FILTERS, 1, batch_normalization=False)

		# Construct the training components.
		self.flattened = tf.reshape(self.flow, [-1, self.OUTPUT_SOFTMAX_COUNT])
		self.flattened_desired_output = tf.reshape(self.desired_policy_ph, [-1, self.OUTPUT_SOFTMAX_COUNT])
		self.cross_entropy = tf.reduce_mean(tf.nn.softmax_cross_entropy_with_logits_v2(
			labels=self.flattened_desired_output,
			logits=self.flattened,
		))
		regularizer = tf.contrib.layers.l2_regularizer(scale=0.0001)
		reg_variables = tf.trainable_variables()
		self.regularization_term = tf.contrib.layers.apply_regularization(regularizer, reg_variables)
		self.loss = self.cross_entropy + self.regularization_term

		# Associate batch normalization with training.
		update_ops = tf.get_collection(tf.GraphKeys.UPDATE_OPS)
		with tf.control_dependencies(update_ops):
			self.train_step = tf.train.MomentumOptimizer(
				learning_rate=self.learning_rate_ph,
				momentum=0.9,
			).minimize(self.loss)

	def stack_convolution(self, kernel_size, old_size, new_size, batch_normalization=True):
		weights = tf.Variable(
			tf.contrib.layers.xavier_initializer_conv2d()(
				[kernel_size, kernel_size, old_size, new_size],
			),
			name="convkernel",
		)
		self.flow = tf.nn.conv2d(
			self.flow,
			weights,
			strides=[1, 1, 1, 1],
			padding="SAME",
		)
		if batch_normalization:
			self.flow = tf.layers.batch_normalization(
				self.flow,
				training=self.is_training_ph,
				momentum=0.999,
#				center=False,
#				scale=False,
#				renorm=True,
#				renorm_momentum=0.999,
			)
		else:
			bias = tf.Variable(tf.constant(0.0, shape=[new_size], dtype=tf.float32), name="bias")
			self.flow = self.flow + bias

	def stack_nonlinearity(self):
		self.flow = tf.nn.relu(self.flow)

	def stack_block(self):
		initial_value = self.flow
		# Stack the first convolution.
		self.stack_convolution(3, self.FILTERS, self.FILTERS)
		self.stack_nonlinearity()
		# Stack the second convolution.
		self.stack_convolution(3, self.FILTERS, self.FILTERS)
		# Add the skip connection.
		self.flow = self.flow + initial_value
		# Stack on the deferred non-linearity.
		self.stack_nonlinearity()

if __name__ == "__main__":
	# Count network parameters.
	net = GoResNet("net/")
	print
	print "Filters:", net.FILTERS
	print "Block count:", net.BLOCK_COUNT

	for var in tf.trainable_variables():
		print var
	parameter_count = sum(np.product(var.shape) for var in tf.trainable_variables())
	print "Parameter count:", parameter_count

