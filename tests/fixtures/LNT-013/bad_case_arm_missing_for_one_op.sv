// sverity-args: --top top
// `carry` is not assigned for op 2: the latch is the case's (the finding is at the first assignment in it, as slang places it).
// expect: LNT-013 WARNING 7
module top(input logic [1:0] op, input logic a, b, output logic carry);
  always_comb begin
    case (op)
      2'd0: carry = a;
      2'd1: carry = b;
      2'd3: carry = a ^ b;
    endcase
  end
endmodule
