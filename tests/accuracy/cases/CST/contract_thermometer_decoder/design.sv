// Authored independently for the accuracy suite.
// Environment: d launches on core; input arrival is [0,2] ns; q required [0,2] ns.
module top(input logic clk, input logic [2:0] d, output logic [7:0] q);

always_ff @(posedge clk) begin q <= (8'h1 << d) - 1'b1; end
endmodule
