// Authored independently for the accuracy suite.
// Environment: d launches on core; input arrival is [0,2] ns; q required [0,2] ns.
module top(input logic clk, input logic [3:0] d, output logic [2:0] q);

always_ff @(posedge clk) begin q <= d[3] ? 3'd4 : d[2] ? 3'd3 : d[1] ? 3'd2 : d[0] ? 3'd1 : 3'd0; end
endmodule
