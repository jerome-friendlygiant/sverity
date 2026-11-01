// sverity-args: --top top
// a continue skips the assignment of y[i]: the latch must not be reported as impossible (Unresolved at least)
// expect: LNT-013 WARNING
module top(input logic [3:0] en, d, output logic [3:0] y);
  always @* begin
    for (int i = 0; i < 4; i++) begin
      if (en[i]) continue;
      y[i] = d[i];
    end
  end
endmodule
