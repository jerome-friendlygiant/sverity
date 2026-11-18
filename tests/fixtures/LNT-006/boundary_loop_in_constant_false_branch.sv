// sverity-args: --top top
// N = 4: the `if (N > 8)` loop reaches index 7 of a four-entry array but never exists in this design. The else branch is live
// and overruns the same way, and is the only finding.
// expect: LNT-006 ERROR 11
module top #(parameter int N = 4)(input logic [3:0] t [4], output logic [7:0] y);
  always_comb begin
    y = '0;
    if (N > 8) begin
      for (int i = 0; i < 8; i++) y[i] = t[i];
    end else begin
      for (int j = 0; j < 8; j++) y[j] = t[j];
    end
  end
endmodule
