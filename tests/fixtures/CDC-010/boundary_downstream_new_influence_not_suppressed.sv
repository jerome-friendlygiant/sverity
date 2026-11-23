// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Adding a third synchronized influence downstream is a distinct reconvergence and is not suppressed as a pure echo.
// expect-count: CDC-010 WARNING 2
module top(input logic clka, clkb, rst_n, input logic [2:0] d, output logic y);
  logic a_q, b_q, c_q, s1a, s2a, s1b, s2b, s1c, s2c, r1, r2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {c_q, b_q, a_q} <= '0; else {c_q, b_q, a_q} <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) begin
    {s2a, s1a, s2b, s1b, s2c, s1c} <= '0;
  end else begin
    {s2a, s1a} <= {s1a, a_q};
    {s2b, s1b} <= {s1b, b_q};
    {s2c, s1c} <= {s1c, c_q};
  end
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) r1 <= 1'b0; else r1 <= s2a & s2b;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) r2 <= 1'b0; else r2 <= r1 & s2c;
  assign y = r2;
endmodule
