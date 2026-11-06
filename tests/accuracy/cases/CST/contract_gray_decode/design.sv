// Authored independently for the accuracy suite.
// Environment: d launches on core; input arrival is [0,2] ns; q required [0,2] ns.
module top(input logic clk, input logic [3:0] d, output logic [3:0] q);

always_ff @(posedge clk) begin q <= {d[3], ^d[3:2], ^d[3:1], ^d}; end
endmodule
