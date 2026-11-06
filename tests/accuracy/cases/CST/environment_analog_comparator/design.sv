// Authored independently for the accuracy suite.
// Environment information for this external interface is deliberately unavailable.
module top(input logic clk, input logic [7:0] d, output logic [7:0] q);
always_ff @(posedge clk) q <= {7'b0,|d};
endmodule
