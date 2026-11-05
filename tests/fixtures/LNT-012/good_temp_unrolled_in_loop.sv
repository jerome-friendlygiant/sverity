// sverity-args: --top top
// The whole temporary is written before the loop reads it, so it never sees its value from before the procedure.
// expect-none: LNT-012
module top(input logic [7:0] q, output logic [7:0] y);
  logic [7:0] s;
  always_comb begin
    s = q;
    for (int i = 0; i < 8; i++) s = {s[0] ^ s[3], s[7:1]};
    y = s;
  end
endmodule
