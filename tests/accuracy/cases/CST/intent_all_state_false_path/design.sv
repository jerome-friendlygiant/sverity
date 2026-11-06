// Authored independently for the accuracy suite.
// Environment: d and q are synchronous core interfaces, active every cycle; no false paths or straps.
module top(input logic clk, input logic [7:0] d, output logic [7:0] q);
logic [7:0] acc=0; always_ff @(posedge clk) begin acc <= acc+d; q <= acc; end
endmodule
