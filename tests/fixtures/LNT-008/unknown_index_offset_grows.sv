// sverity-args: --top top
// The index is the loop variable plus a block-local offset that grows: slang reports the whole vector driven, but only the
// even bits are written.
// expect: LNT-008 WARNING 6
module top(input logic c, output logic [7:0] y);
  logic [7:0] v;
  always_comb begin
    automatic int off = 0;
    for (int k = 0; k < 4; k++) begin v[k + off] = c; off = off + 1; end
  end
  assign y = v;
endmodule
