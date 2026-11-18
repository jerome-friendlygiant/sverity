// sverity-args: --top top
// One combinational process per generate share writes its own word, with a loop over the packed part of it: the words differ by the genvar.
// expect: LNT-049 INFO 6
// expect-none: LNT-049 WARNING
module top(input logic [3:0][7:0] key [2], output logic [3:0][7:0] y);
  logic [3:0][7:0] r [2];
  for (genvar s = 0; s < 2; s++) begin : gen_share
    always_comb for (int i = 0; i < 4; i++) r[s][i] = key[s][i] ^ 8'h5a;
  end
  assign y = r[0] ^ r[1];
endmodule
