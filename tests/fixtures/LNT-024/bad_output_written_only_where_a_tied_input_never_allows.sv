// sverity-args: --top top
// The only instance ties en to 0: the write to the output port is dead code, so the output has no driver in hardware.
// expect: LNT-024 WARNING 5
module leaf(input logic clk, input logic [1:0] en, input logic [7:0] d,
            output logic [7:0] q);
  always_ff @(posedge clk) if (en == 1) q <= d;
endmodule
module top(input logic clk, input logic [7:0] d, output logic [7:0] q);
  leaf u(.clk, .en(2'd0), .d, .q);
endmodule
