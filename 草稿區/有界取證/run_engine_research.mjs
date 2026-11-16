import { research } from './engine_research/controller.mjs';
const [mode,scopePath,output]=process.argv.slice(2);
const receipt=await research({mode,scopePath,output,
  ...(process.env.GE_RESEARCH_RUNTIME?{runtimeRoot:process.env.GE_RESEARCH_RUNTIME}:{}),
  ...(process.env.GE_RESEARCH_PYTHON?{python:process.env.GE_RESEARCH_PYTHON}:{}),
  onProgress:e=>console.log(JSON.stringify(e))});
console.log(JSON.stringify({status:receipt.status,dataQualified:receipt.dataQualified,resourceRelease:receipt.resourceRelease,output}));
if(['FAILED','INCOMPLETE'].includes(receipt.status))process.exitCode=1;
