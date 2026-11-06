// Authored independently for the accuracy suite.
// Environment: d and q are synchronous core interfaces, active every cycle; no false paths or straps.
module top(input logic clk, input logic [7:0] d, output logic [7:0] q);
logic [7:0] pipe; always_ff @(posedge clk) begin pipe <= d; q <= pipe; end
endmodule
