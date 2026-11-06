module top (
  input  logic clk_i,
  input  logic por_ni,
  input  logic watchdog_trip_i,
  input  logic [7:0] command_i,
  output logic [7:0] status_o
);
  logic [1:0] release_q;
  logic       reset_ni;
  logic [7:0] status_q;

  always_ff @(posedge clk_i or negedge por_ni) begin
    if (!por_ni) release_q <= 2'b00;
    else release_q <= {release_q[0], 1'b1};
  end

  // The watchdog is asynchronous and bypasses the release synchronizer.
  assign reset_ni = release_q[1] & ~watchdog_trip_i;

  always_ff @(posedge clk_i or negedge reset_ni) begin
    if (!reset_ni) status_q <= 8'h00;
    else status_q <= command_i ^ 8'hc3;
  end

  assign status_o = status_q;
endmodule
