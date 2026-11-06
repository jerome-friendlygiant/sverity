// Authored independently for the accuracy suite.
// Environment: d and q are synchronous core interfaces, active every cycle; no false paths or straps.
module top(input logic clk, input logic [7:0] d, output logic [7:0] q);
logic [7:0] stage; always_ff @(posedge clk) begin stage <= d; q <= stage+1'b1; end
endmodule
