// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Parameter EN=0 prunes the non-zero arm: src_q and tog_q stay constant 0 across all live writes.
// expect-none: CDC-013
// expect-none: CDC-014
// expect-none: CDC-024
// expect-none: RDC-023
module top #(parameter bit EN = 1'b0) (
  input logic clka, clkb, rst_a, s_rst, d, output logic [1:0] q
);
  logic src_q, tog_q, s1, s2, t1, t2;
  always_ff @(posedge clka or posedge rst_a) begin
    if (rst_a) src_q <= 1'b0;
    else if (EN) src_q <= d;
    else src_q <= 1'b0;
  end
  always_ff @(posedge clka) begin
    tog_q <= tog_q ^ src_q;
    if (s_rst) tog_q <= 1'b0;
  end
  always_ff @(posedge clkb or posedge rst_a) begin
    if (rst_a) begin
      {s2, s1} <= '0;
      {t2, t1} <= '0;
    end else begin
      {s2, s1} <= {s1, src_q};
      {t2, t1} <= {t1, tog_q};
    end
  end
  assign q = {s2, t2};
endmodule
