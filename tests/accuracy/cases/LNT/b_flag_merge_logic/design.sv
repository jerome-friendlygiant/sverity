// Merges receive-path error flags into one status word and an interrupt request.
// Interface:
//   parity_err, crc_err, overrun  raw error flags
//   mask_en, err_mask             optional per-flag mask (1 = report)
//   status                        {any_err, masked flags}
module top (
  input  logic       parity_err,
  input  logic       crc_err,
  input  logic       overrun,
  input  logic       mask_en,
  input  logic [2:0] err_mask,
  output logic       any_err,
  output logic       irq_req,
  output logic [3:0] status
);
  logic [2:0] raw;
  logic [2:0] masked;

  assign raw     = {overrun, crc_err, parity_err};
  assign masked  = raw & (mask_en ? err_mask : 3'b111);

  // error summary also reports an interrupt that is already pending
  assign any_err = (|masked) | irq_req;
  assign irq_req = any_err & mask_en;

  assign status  = {any_err, masked};
endmodule
