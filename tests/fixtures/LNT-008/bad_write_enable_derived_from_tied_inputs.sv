// sverity-args: --top top
// The write is guarded by a net that is the AND of two inputs; the only instance ties one of them to 0, so the net is 0 and r is never driven.
// expect: LNT-008 WARNING 5
module child(input logic clk, en, mode, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] r;
  wire go = en & mode;
  always_ff @(posedge clk) if (go) r <= d;
  assign q = r;
endmodule
module top(input logic clk, mode, input logic [7:0] d, output logic [7:0] q);
  child u(.clk, .en(1'b0), .mode, .d, .q);
endmodule
