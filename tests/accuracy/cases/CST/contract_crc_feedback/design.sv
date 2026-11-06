// Authored independently for the accuracy suite.
// Environment: d launches on core; input arrival is [0,2] ns; q required [0,2] ns.
module top(input logic clk, input logic d, output logic [7:0] q);
initial q=0;
always @(posedge clk) begin q <= {q[6:0],d ^ q[7]} ^ (q[7] ? 8'h07 : 8'h00); end
endmodule
