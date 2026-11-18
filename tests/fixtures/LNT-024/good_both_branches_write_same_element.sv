// sverity-args: --top top
// An if/else writes element i in each arm, so every iteration makes the write.
// expect-none: LNT-024
module top(input logic c, output logic [3:0] o);
  always_comb begin
    for (int i = 0; i < 4; i++) begin
      if (c) o[i] = 1'b1;
      else o[i] = 1'b0;
    end
  end
endmodule
