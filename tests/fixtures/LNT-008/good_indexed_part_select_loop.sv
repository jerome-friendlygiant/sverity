// sverity-args: --top top
// A loop writes v[32*i +: 32] for every word of the vector.
// expect-none: LNT-008
module top #(parameter int NW = 2) (input logic [31:0] a [NW], output logic [NW*32-1:0] y);
  logic [NW*32-1:0] v;
  always_comb begin
    for (int i = 0; i < NW; i++) v[32*i +: 32] = a[i];
  end
  assign y = v;
endmodule
