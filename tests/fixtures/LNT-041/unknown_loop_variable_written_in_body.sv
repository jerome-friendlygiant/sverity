// sverity-args: --top top
// The loop variable is also advanced in the body, so the loop does not visit every element of the array.
// expect: LNT-041 WARNING 5
module top(output logic [7:0] y);
  logic [3:0][1:0] v;
  always_comb begin
    for (int i = 0; i < 4; i++) begin
      v[i] = 2'b11;
      i++;
    end
  end
  assign y = v;
endmodule
