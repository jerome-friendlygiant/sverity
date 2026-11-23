// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// An unsynchronised crossing from a constant-stuck source flop still requires synchroniser stages (CDC-003 ERROR).
// expect: CDC-003 ERROR
module top(input logic clka, clkb, rst_a, output logic q);
  logic stuck_q;
  always_ff @(posedge clka or posedge rst_a) begin
    if (rst_a) stuck_q <= 1'b0;
    else stuck_q <= 1'b0;
  end
  always_ff @(posedge clkb or posedge rst_a) begin
    if (rst_a) q <= 1'b0;
    else q <= stuck_q;
  end
endmodule
