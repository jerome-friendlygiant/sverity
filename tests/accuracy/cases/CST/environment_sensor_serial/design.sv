// Authored independently for the accuracy suite.
// Environment information for this external interface is deliberately unavailable.
module top(input logic clk, input logic [7:0] d, output logic [7:0] q);
initial q=0;
always @(posedge clk) q <= {q[6:0],d[0]};
endmodule
