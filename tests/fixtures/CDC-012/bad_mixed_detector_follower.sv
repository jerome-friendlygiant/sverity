// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --profile fpga
// expect: CDC-012 WARNING
// The delayed copy feeds both the detector and the remaining synchronizer chain.
module top(input logic clka, clkb, d, output logic pulse, filtered);
  logic launch, prior;
  (* ASYNC_REG = "TRUE" *) logic s1, s2, s4;
  always_ff @(posedge clka) launch <= d;
  always_ff @(posedge clkb) begin
    s1 <= launch;
    s2 <= s1;
    prior <= s2;
    s4 <= prior;
  end
  assign pulse = s2 ^ prior;
  assign filtered = s4;
endmodule
