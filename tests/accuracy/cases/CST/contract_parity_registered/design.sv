// Authored independently for the accuracy suite.
// Environment: d launches on core; input arrival is [0,2] ns; q required [0,2] ns.
module top(input logic clk, input logic [7:0] d, output logic q);

always_ff @(posedge clk) begin q <= ^d; end
endmodule
