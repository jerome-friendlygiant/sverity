module top (
  input logic sample_ref,
  input logic debug_ref,
  input logic [7:0] sensor,
  output logic [7:0] reading,
  output logic debug_heartbeat
);
  wire sample_clk;
  assign sample_clk = ~sample_ref;
  always_ff @(posedge sample_clk)
    reading <= sensor;
  always_ff @(posedge debug_ref)
    debug_heartbeat <= ~debug_heartbeat;
endmodule
