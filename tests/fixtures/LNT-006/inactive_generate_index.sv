// sverity-args: --top top
// expect-count: LNT-006 ERROR 1
module top(input logic [3:0] x, output logic y);
  if (0) begin : inactive
    logic dead;
    always_comb begin
      dead = 0;
      for (int i = 0; i <= 4; i++) dead ^= x[i];
    end
  end
  always_comb begin
    y = 0;
    for (int i = 0; i <= 4; i++) y ^= x[i];
  end
endmodule
