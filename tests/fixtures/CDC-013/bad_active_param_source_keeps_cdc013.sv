// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc -G EN=1
// Parameter EN=1 keeps the dynamic write live, so a stuck-constant exemption must not apply.
// expect: CDC-013 WARNING
module top #(parameter bit EN = 1'b0) (
  input logic clka, clkb, rst_a, d, output logic q
);
  logic src_q, s1, s2;
  always_ff @(posedge clka or posedge rst_a) begin
    if (rst_a) src_q <= 1'b0;
    else if (EN) src_q <= d;
    else src_q <= 1'b0;
  end
  always_ff @(posedge clkb or posedge rst_a) begin
    if (rst_a) {s2, s1} <= '0;
    else {s2, s1} <= {s1, src_q};
  end
  assign q = s2;
endmodule
