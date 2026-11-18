// sverity-args: --top top
// The loop variable is also advanced by a ref argument, so the loop does not visit every index of the vector.
// expect: LNT-008 WARNING 5
module top(output logic [7:0] y);
  logic [7:0] v;
  task automatic bump(ref int k); k = k + 1; endtask
  always_comb begin
    for (int i = 0; i < 8; i++) begin
      v[i] = 1'b1;
      bump(i);
    end
  end
  assign y = v;
endmodule
