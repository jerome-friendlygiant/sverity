// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A 64-stage synchroniser chain: the source is reached on the last allowed hop, so the trace is complete and no trace bound is reported.
// expect-confidence: RDC-013 informational
module top(input logic clka, clkb, rst_n, d, output logic q);
  wire [64:0] root_n;
  assign root_n[0] = rst_n;
  for (genvar i = 0; i < 64; ++i) begin : g
    logic [1:0] sync;
    always_ff @(posedge clka or negedge root_n[i]) if (!root_n[i]) sync <= '0; else sync <= {sync[0], 1'b1};
    assign root_n[i+1] = sync[1];
  end
  always_ff @(posedge clka or negedge root_n[64]) if (!root_n[64]) q <= 1'b0; else q <= d;
endmodule
