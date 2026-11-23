// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// An incrementing counter with constant reset is active, never constant-stuck.
// expect: CDC-013 WARNING
module top(input logic clka, clkb, rst_a, output logic q);
  logic [1:0] cnt;
  logic s1, s2;
  always_ff @(posedge clka or posedge rst_a) begin
    if (rst_a) cnt <= 2'b00;
    else cnt++;
  end
  always_ff @(posedge clkb or posedge rst_a) begin
    if (rst_a) {s2, s1} <= '0;
    else {s2, s1} <= {s1, cnt[0]};
  end
  assign q = s2;
endmodule
