// sverity-args: --top top
// The selector is tied to 1 and the label is 2'd2: the default arm runs and writes r.
// expect-none: LNT-008
module child(input logic clk, en, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] r;
  always_ff @(posedge clk) case (en) 2'd2: ; default: r <= d; endcase
  assign q = r;
endmodule
module top(input logic clk, input logic [7:0] d, output logic [7:0] q);
  child u(.clk, .en(1'b1), .d, .q);
endmodule
