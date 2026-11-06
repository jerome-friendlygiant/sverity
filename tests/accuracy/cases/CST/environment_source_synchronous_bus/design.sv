// Authored independently for the accuracy suite.
// Environment information for this external interface is deliberately unavailable.
module top(input logic clk, input logic [7:0] d, output logic [7:0] q);
always_ff @(posedge clk) q <= d ^ 8'h80;
endmodule
