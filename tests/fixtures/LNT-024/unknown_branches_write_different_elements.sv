// sverity-args: --top top
// The else arm writes element 3-i, not element i: the write of element i is not made on every iteration.
// expect: LNT-024 WARNING 4
module top(input logic c, output logic [3:0] o);
  always_comb begin
    for (int i = 0; i < 4; i++) begin
      if (c) o[i] = 1'b1;
      else o[3 - i] = 1'b0;
    end
  end
endmodule
