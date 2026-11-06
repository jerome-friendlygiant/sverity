// Authored independently for the accuracy suite.
// Environment: d launches on core; input arrival is [0,2] ns; q required [0,2] ns.
module top(input logic clk, input logic d, output logic [7:0] q);
initial q = '0;
always @(posedge clk) begin q <= (&q) ? q : q + {7'b0,d}; end
endmodule
