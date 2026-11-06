// Authored independently for the accuracy suite.
// Environment: d launches on core; input arrival is [0,2] ns; q required [0,2] ns.
module top(input logic clk, input logic d, output logic q);
initial q=0;
always @(posedge clk) begin q <= q | d; end
endmodule
