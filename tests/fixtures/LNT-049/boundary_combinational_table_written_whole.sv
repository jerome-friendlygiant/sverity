// sverity-args: --top top
// The procedure writes the whole array before the run-time write, so it holds no state between activations: a combinational table.
// expect: LNT-049 INFO 6
// expect-none: LNT-049 WARNING
module top(input logic [1:0] wa, input logic [7:0] wd, input logic [1:0] ra, output logic [7:0] rd);
  logic [7:0] m [4];
  always_comb begin
    m = '{default: 8'd0};
    m[wa] = wd;
  end
  assign rd = m[ra];
endmodule
