// sverity-args: --top top
// `==?` with a signal on the right: its X/Z bits are the wildcards in simulation only.
// expect: LNT-040 WARNING 7
// revert: (d ==? pat) => (d ==? 8'b1010_????)
// revert-expect-none: LNT-040
module top(input logic [7:0] d, pat, output logic m);
  assign m = (d ==? pat);
endmodule
