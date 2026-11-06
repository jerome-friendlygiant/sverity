// Authored independently for the accuracy suite.
// Environment: d launches on core; input arrival is [0,2] ns; q required [0,2] ns.
module top(input logic clk, input logic d, output logic q);
logic last=0;
always_ff @(posedge clk) begin q <= d & ~last; last <= d; end
endmodule
