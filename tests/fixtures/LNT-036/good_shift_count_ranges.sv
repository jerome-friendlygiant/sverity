// sverity-args: --top top
// expect-none: LNT-036
module top(input logic [2:0] level, output logic [7:0] a, b, c, d);
  assign a = 8'd1 << (level + 4'd1); // four-bit count holds every three-bit level plus one
  assign b = 8'd1 << (4'(level + 3'd1)); // explicit wide cast evaluates the addition wide
  always_comb begin
    c = '0;
    for (logic [2:0] i = 0; i < 4; i++)
      c = 8'd1 << (i + 3'd1); // the proven loop range is 0..3, so this three-bit sum fits
  end
  assign d = 8'd1 << ((level & 3'b110) + 3'd1); // the mask bounds the addend to 0..6
endmodule
