// sverity-args: --top top --sdc @FIXTURES@/CDC-003/excl.sdc
// The exact minimum depth (two stages) is enough to trigger the exclusive-clock contradiction.
// expect: CDC-037 WARNING
module top(input logic clka, clkb, d, output logic q);
  logic launched, stage1;
  always_ff @(posedge clka) launched <= d;
  always_ff @(posedge clkb) begin
    stage1 <= launched;
    q <= stage1;
  end
endmodule
