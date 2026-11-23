// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Combinational self-rewrite w = stuck_q; w = ~w; stops alias traversal and keeps toggle CDC-014/013 active.
// expect: CDC-014 WARNING
// expect: CDC-013 WARNING
module top(input logic clka, clkb, rst_a, output logic q);
  logic stuck_q, w, tog_q, s1, s2;
  always_ff @(posedge clka or posedge rst_a) begin
    if (rst_a) stuck_q <= 1'b0;
    else stuck_q <= 1'b0;
  end
  always_comb begin
    w = stuck_q;
    w = ~w;
  end
  always_ff @(posedge clka or posedge rst_a) begin
    if (rst_a) tog_q <= 1'b0;
    else tog_q <= tog_q ^ w;
  end
  always_ff @(posedge clkb or posedge rst_a) begin
    if (rst_a) {s2, s1} <= '0;
    else {s2, s1} <= {s1, tog_q};
  end
  assign q = s2;
endmodule
