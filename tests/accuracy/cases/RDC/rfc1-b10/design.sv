// Latches a fault flag that the watchdog sets asynchronously and software clears.
module top (
  input  logic clk,
  input  logic rst_ni,
  input  logic wdog_fault_i,
  input  logic sw_clr_i,
  output logic fault_o
);
  logic fault_q;

  always_ff @(posedge clk or negedge rst_ni or posedge wdog_fault_i)
    if (!rst_ni)       fault_q <= 1'b0;
    else if (wdog_fault_i) fault_q <= 1'b1;
    else if (sw_clr_i) fault_q <= 1'b0;

  assign fault_o = fault_q;
endmodule
