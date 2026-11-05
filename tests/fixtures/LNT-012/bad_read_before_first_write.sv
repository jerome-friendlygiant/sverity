// sverity-args: --top top
// Lookalike of good_temp_unrolled_in_loop without the initial write: the first read sees the previous value.
// expect: LNT-012 WARNING
module top(input logic [7:0] q, output logic [7:0] y);
  logic [7:0] s;
  always_comb begin
    s = {s[0] ^ s[3], s[7:1]} ^ q;
    y = s;
  end
endmodule
